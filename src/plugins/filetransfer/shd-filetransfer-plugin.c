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

#include <string.h>

#include "shd-filetransfer.h"

/* create a new node using this plug-in */
static void filetransferplugin_new(int argc, char* argv[]) {
	filetransfer_new(argc, argv);
}

static void filetransferplugin_free() {
	filetransfer_free();
}

static void filetransferplugin_activate() {
	filetransfer_activate();
}

/* my global structure to hold all variable, node-specific application state */
FileTransfer filetransferplugin_globalData;

/* shadow calls this function for a one-time initialization
 *
 * !WARNING! dont malloc() (or g_new()) anything until filetransferplugin_new
 * unless that memory region is registered with shadow by giving a pointer to it.
 * its better to register as little as possible because everything that is
 * registered is copied on every shadow-to-plugin context switch.
 */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* clear our memory before initializing */
	memset(&filetransferplugin_globalData, 0, sizeof(FileTransfer));

	/* save the shadow functions we will use since it will be the same for all nodes */
	filetransferplugin_globalData.shadowlib = shadowlibFuncs;

	/* give the filetransfer code a pointer to this global struct. this allows
	 * access to our FileTransfer struct without needing to 'extern' it.
	 */
	filetransfer_init(&filetransferplugin_globalData);

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = shadowlibFuncs->registerPlugin(&filetransferplugin_new, &filetransferplugin_free, &filetransferplugin_activate);
	if(success) {
		shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered filetransfer plug-in state");
	} else {
		shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "error registering filetransfer plug-in state");
	}
}
