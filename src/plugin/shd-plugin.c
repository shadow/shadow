/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"

Plugin* plugin_new(GString* filename) {
	g_assert(filename);
	Plugin* plugin = g_new0(Plugin, 1);
	MAGIC_INIT(plugin);

	/* get the plugin handle */
	plugin->handle = g_module_open(filename->str, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if(!plugin->handle) {
		error("unable to load plug-in '%s'", filename->str);
	}

	/* make sure it has the required init function */
	gpointer func;
	gboolean success = g_module_symbol(plugin->handle, PLUGININITSYMBOL, &func);
	if(success) {
		plugin->init = func;
	} else {
		error("unable to find the required function symbol '%s' in plug-in '%s'",
				PLUGININITSYMBOL, filename->str);
	}

	/* notify the plugin of our callable functions by calling the init func */
	// TODO need to go into plugin context mode
	plugin->init(&shadowlibVTable);

	return plugin;
}

void plugin_free(gpointer data) {
	Plugin* plugin = data;
	MAGIC_ASSERT(plugin);

	if(plugin->handle) {
		gboolean success = g_module_close(plugin->handle);
		/* TODO: what to do if failure? */
		if(!success) {
			warning("failed closing plugin '%s'", plugin->path);
		}
	}

	MAGIC_CLEAR(plugin);
	g_free(plugin);
}

void plugin_registerResidentState(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);
	if(plugin->isRegisterred) {
		warning("ignoring duplicate state registration");
		return;
	}


	plugin->isRegisterred = TRUE;
}

void plugin_loadState(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);

}

void plugin_saveState(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);

}

void plugin_execute(Plugin* plugin) {
	Worker* worker = worker_getPrivate();
	worker->cached_plugin = plugin;

	// TODO call whatever function

	worker->cached_plugin = NULL;
}
