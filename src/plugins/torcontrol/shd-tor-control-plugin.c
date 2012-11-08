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

#include "shd-tor-control.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
TorControl torControlState;

void torControlPlugin_new(int argc, char* argv[]) {
	if(argc < 2) {
		const gchar* USAGE = "TorControl USAGE: controlHostsFile ('hostname:port mode [modeArgs]')\n"
					"\t'circuitBuild hop1 hop2 ... -1'\n";
		torControlState.shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "%s", USAGE);
		return;
	}

	TorControl_Args args;
	args.hostsFilename = g_strdup(argv[1]);

	torControl_new(&args);

	g_free(args.hostsFilename);
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
		torControlState.shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"successfully registered ping plug-in state");
	} else {
		torControlState.shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error registering ping plug-in state");
	}
}
