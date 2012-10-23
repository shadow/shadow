/*
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

#include "shd-browser.h"

/* my global structure to hold all variable, node-specific application state */
browser_t b;

/* create a new node using this plug-in */
static void browserplugin_new(int argc, char* argv[]) {
	browser_start(&b, argc, argv);
}

static void browserplugin_free() {
	browser_free(&b);
}

static void browserplugin_activate() {
	if(!b.epolld) {
		b.shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "client cant wait on epoll without epoll descriptor");
	} else {
		struct epoll_event events[10];
		int nfds = epoll_wait(b.epolld, events, 10, 0);
		if(nfds == -1) {
			b.shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
		} else {
			/* finally, activate client for every socket thats ready */
			for(int i = 0; i < nfds; i++) {
				browser_activate(&b, events[i].data.fd);
			}
		}
	}
}

PluginFunctionTable browser_pluginFunctions = {
	&browserplugin_new, &browserplugin_free,
	&browserplugin_activate,
};

/* shadow calls this function for a one-time initialization
 *
 * !WARNING! dont malloc() (or g_new()) anything until filetransferplugin_new
 * unless that memory region is registered with shadow by giving a pointer to it.
 * its better to register as little as possible because everything that is
 * registered is copied on every shadow-to-plugin context switch.
 */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* clear our memory before initializing */
	memset(&b, 0, sizeof(browser_t));

	/* save the shadow functions we will use since it will be the same for all nodes */
	b.shadowlib = shadowlibFuncs;

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = shadowlibFuncs->registerPlugin(&browser_pluginFunctions, 1,
			sizeof(browser_t), &b);
	if(success) {
		shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered browser plug-in state");
	} else {
		shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "error registering browser plug-in state");
	}
}
