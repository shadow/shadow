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

#include "shd-ping.h"


void pingPlugin_new(int argc, char* argv[]) {
	ping_new(argc, argv);
}

void pingPlugin_free() {
	ping_free();
}

void pingPlugin_activate() {
	ping_activate();
}

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Ping pingState;

void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);

	/* start out with cleared state */
	memset(&pingState, 0, sizeof(Ping));

	/* save the functions Shadow makes available to us */
	pingState.shadowlib = shadowlibFuncs;

	Ping** pingGlobalPointer = ping_init(&pingState);

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = pingState.shadowlib->registerPlugin(&pingPlugin_new, &pingPlugin_free, &pingPlugin_activate);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		pingState.shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered ping plug-in state");
	} else {
		pingState.shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering ping plug-in state");
	}
}
