/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shd-echo.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Echo echostate;

void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);

	/* start out with cleared state */
	memset(&echostate, 0, sizeof(Echo));

	/* save the functions Shadow makes available to us */
	echostate.shadowlibFuncs = *shadowlibFuncs;

	/*
	 * we 'register' our function table, telling shadow which of our functions
	 * it can use to notify our plugin
	 */
	gboolean success = echostate.shadowlibFuncs.registerPlugin(&echoplugin_new, &echoplugin_free, &echoplugin_ready);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		echostate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered echo plug-in state");
	} else {
		echostate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering echo plug-in state");
	}
}

void echoplugin_new(int argc, char* argv[]) {
	echostate.shadowlibFuncs.log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"echoplugin_new called");

	const char* USAGE = "Echo USAGE: 'tcp client serverIP', 'tcp server', 'tcp loopback', 'tcp socketpair', "
			"'udp client serverIP', 'udp server', 'udp loopback', 'pipe'\n"
			"** clients and servers must be paired together, but loopback, socketpair,"
			"and pipe modes stand on their own.";


	/* 0 is the plugin name, 1 is the protocol */
	if(argc < 2) {
		echostate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
		return;
	}

	char* protocol = argv[1];

	gboolean isError = TRUE;

	/* check for the protocol option and create the correct application state */
	if(g_ascii_strncasecmp(protocol, "tcp", 3) == 0)
	{
		echostate.protocol = ECHOP_TCP;
		echostate.etcp = echotcp_new(echostate.shadowlibFuncs.log, argc - 2, &argv[2]);
		isError = (echostate.etcp == NULL) ? TRUE : FALSE;
	}
	else if(g_ascii_strncasecmp(protocol, "udp", 3) == 0)
	{
		echostate.protocol = ECHOP_UDP;
		echostate.eudp = echoudp_new(echostate.shadowlibFuncs.log, argc - 2, &argv[2]);
		isError = (echostate.eudp == NULL) ? TRUE : FALSE;
	}
	else if(g_ascii_strncasecmp(protocol, "pipe", 4) == 0)
	{
		echostate.protocol = ECHOP_PIPE;
		echostate.epipe = echopipe_new(echostate.shadowlibFuncs.log);
		isError = (echostate.epipe == NULL) ? TRUE : FALSE;
	}

	if(isError) {
		/* unknown argument for protocol, log usage information through Shadow */
		echostate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
	}
}

void echoplugin_free() {
	echostate.shadowlibFuncs.log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "echoplugin_free called");

	/* call the correct version depending on what we are running */
	switch(echostate.protocol) {
		case ECHOP_TCP: {
			echotcp_free(echostate.etcp);
			break;
		}

		case ECHOP_UDP: {
			echoudp_free(echostate.eudp);
			break;
		}

		case ECHOP_PIPE: {
			echopipe_free(echostate.epipe);
			break;
		}

		default: {
			echostate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"unknown protocol in echoplugin_free");
			break;
		}
	}
}

void echoplugin_ready() {
	echostate.shadowlibFuncs.log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "echoplugin_ready called");

	/* call the correct version depending on what we are running */
	switch(echostate.protocol) {
		case ECHOP_TCP: {
			echotcp_ready(echostate.etcp);
			break;
		}

		case ECHOP_UDP: {
			echoudp_ready(echostate.eudp);
			break;
		}

		case ECHOP_PIPE: {
			echopipe_ready(echostate.epipe);
			break;
		}

		default: {
			echostate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"unknown protocol in echoplugin_ready");
			break;
		}
	}
}
