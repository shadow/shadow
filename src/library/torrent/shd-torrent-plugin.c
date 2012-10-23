/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

/* function table for Shadow so it knows how to call us */
PluginFunctionTable torrent_pluginFunctions = {
	&torrentPlugin_new, &torrentPlugin_free, &torrentPlugin_activate,
};

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
	 *
	 * we 'register' our function table, and 1 variable.
	 */
	gboolean success = torrentState.shadowlib->registerPlugin(&torrent_pluginFunctions, 2,
			sizeof(Torrent), &torrentState,
			sizeof(Torrent*), torrentGlobalPointer);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		torrentState.shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered torrent plug-in state");
	} else {
		torrentState.shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering torrent plug-in state");
	}
}
