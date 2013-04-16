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
		torrentState.shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered torrent plug-in state");
	} else {
		torrentState.shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering torrent plug-in state");
	}
}
