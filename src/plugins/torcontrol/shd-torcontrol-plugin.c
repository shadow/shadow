/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shd-torcontrol.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
TorControl torControlState;

void torControlPlugin_new(int argc, char* argv[]) {
	if(argc < 2) {
		const gchar* USAGE = "TorControl USAGE:\n"
				"\tsingle hostname port [module moduleArgs]\n"
				"\tmulti controlHostsFile\n\n"
				"available modules:\n"
				"\t'circuitBuild node1,node2,...,nodeN'\n"
				"\t'log event1,event2,...,eventN'\n";
		torControlState.shadowlib->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "%s", USAGE);
		return;
	}

	/* argv[0] is our plugin name */
	TorControl_Args args;
	args.mode = g_strdup(argv[1]);
	args.argc = 0;
	args.argv = NULL;
	if(argc > 2) {
		args.argv = &argv[2];
		args.argc = argc - 2;
	}

	torControl_new(&args);

	g_free(args.mode);
}

void torControlPlugin_free() {
	torControl_free();
}

void torControlPlugin_activate() {
	torControl_activate();
}

void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);

	/* start out with cleared state */
	memset(&torControlState, 0, sizeof(TorControl));

	/* save the functions Shadow makes available to us */
	torControlState.shadowlib = shadowlibFuncs;

	torControl_init(&torControlState);

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 *
	 * we 'register' our function table, and 1 variable.
	 */
	gboolean success = torControlState.shadowlib->registerPlugin(&torControlPlugin_new,
			&torControlPlugin_free, &torControlPlugin_activate);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		torControlState.shadowlib->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered ping plug-in state");
	} else {
		torControlState.shadowlib->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering ping plug-in state");
	}
}
