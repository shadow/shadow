/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
		pingState.shadowlib->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered ping plug-in state");
	} else {
		pingState.shadowlib->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering ping plug-in state");
	}
}
