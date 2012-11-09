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
