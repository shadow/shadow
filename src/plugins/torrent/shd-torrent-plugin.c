/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shd-torrent.h"


void torrentPlugin_new(int argc, char* argv[]) {
	torrent_new(argc, argv);
}

void torrentPlugin_free() {
	torrent_free();
}

void torrentPlugin_activate() {
	torrent_activate();
}

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Torrent torrentState;

void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);

	/* start out with cleared state */
	memset(&torrentState, 0, sizeof(Torrent));

	/* save the functions Shadow makes available to us */
	torrentState.shadowlib = shadowlibFuncs;

	Torrent** torrentGlobalPointer = torrent_init(&torrentState);

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = torrentState.shadowlib->registerPlugin(&torrentPlugin_new, &torrentPlugin_free, &torrentPlugin_activate);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		torrentState.shadowlib->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered torrent plug-in state");
	} else {
		torrentState.shadowlib->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering torrent plug-in state");
	}
}
