/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shd-echo.h"

/**
 * If a module contains a function named g_module_check_init() it is called
 * automatically when the module is loaded. It is passed the GModule structure
 * and should return NULL on success or a string describing the initialization
 * error.
 * @param module :
 *	 the GModule corresponding to the module which has just been loaded.
 * Returns :
 *   NULL on success, or a string describing the initialization error.
 *
 *   g_module_unload(GModule* module) is called by glib right before its unloaded.
 *
 */

PluginFunctionTable echo_pluginFunctions = {
	&echo_new, &echo_free, &echo_ready,
};

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Echo echo_globalState;

void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	echo_globalState.shadowlibFuncs = shadowlibFuncs;

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = shadowlibFuncs->registration(&echo_pluginFunctions, 1, sizeof(Echo), &echo_globalState);
	if(success) {
		shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered echo plug-in state");
	} else {
		shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "error registering echo plug-in state");
	}
}

void echo_new(int argc, char* argv[]) {
	echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "echo_new called");

	echo_globalState.client = NULL;
	echo_globalState.server = NULL;

	char* USAGE = "Echo usage: 'client serverHostname', 'server', or 'loopback'";
	if(argc < 1) {
		echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
		return;
	}

	/* parse command line args */
	char* mode = argv[0];
	char* protostr = argv[1];

	enum EchoProtocol protocol = g_strcasecmp(protostr, "tcp") == 0 ? EchoTCP :
			g_strcasecmp(protostr, "udp") == 0 ? EchoUDP : EchoPIPE;

	if(g_strcasecmp(mode, "client") == 0) {
		if(argc < 3) {
			echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
			return;
		}

		/* start up a client, connecting to the server specified in args */
		char* serverHostname = argv[2];
		in_addr_t serverIP = echo_globalState.shadowlibFuncs->resolveHostname(serverHostname);
		echo_globalState.client = echoclient_new(protocol, serverIP, echo_globalState.shadowlibFuncs->log);
	} else if(g_strcasecmp(mode, "server") == 0) {
		in_addr_t serverIP = echo_globalState.shadowlibFuncs->getIP();
		echo_globalState.server = echoserver_new(protocol, serverIP, echo_globalState.shadowlibFuncs->log);
	} else if(g_strcasecmp(mode, "loopback") == 0) {
		echo_globalState.server = echoserver_new(protocol, htonl(INADDR_LOOPBACK), echo_globalState.shadowlibFuncs->log);
		echo_globalState.client = echoclient_new(protocol, htonl(INADDR_LOOPBACK), echo_globalState.shadowlibFuncs->log);
	} else {
		echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
	}
}

void echo_free() {
	echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "echo_free called");

	if(echo_globalState.client) {
		echoclient_free(echo_globalState.client);
	}

	if(echo_globalState.server) {
		echoserver_free(echo_globalState.server);
	}
}

void echo_ready() {
	echo_globalState.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "echo_ready called");

	if(echo_globalState.client) {
		echoclient_ready(echo_globalState.client, echo_globalState.shadowlibFuncs->log);
	}

	if(echo_globalState.server) {
		echoserver_ready(echo_globalState.server, echo_globalState.shadowlibFuncs->log);
	}
}
