/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
		b.shadowlib->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "client cant wait on epoll without epoll descriptor");
	} else {
		struct epoll_event events[10];
		int nfds = epoll_wait(b.epolld, events, 10, 0);
		if(nfds == -1) {
			b.shadowlib->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
		} else {
			/* finally, activate client for every socket thats ready */
			for(int i = 0; i < nfds; i++) {
				browser_activate(&b, events[i].data.fd);
			}
		}
	}
}

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
	gboolean success = shadowlibFuncs->registerPlugin(&browserplugin_new, &browserplugin_free, &browserplugin_activate);
	if(success) {
		shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered browser plug-in state");
	} else {
		shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "error registering browser plug-in state");
	}
}
