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

Plugin* plugin_new(GQuark id, GString* filename) {
	g_assert(filename);
	Plugin* plugin = g_new0(Plugin, 1);
	MAGIC_INIT(plugin);

	plugin->id = id;

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

	/* notify the plugin of our callable functions by calling the init function,
	 * this is a special version of executing because we still dont know about
	 * the plug-in libraries state. */
	Worker* worker = worker_getPrivate();
	plugin->isExecuting = TRUE;
	worker->cached_plugin = plugin;
	plugin_setShadowContext(plugin, FALSE);
	plugin->init(&shadowlibFunctionTable);
	plugin_setShadowContext(plugin, TRUE);
	plugin->isExecuting = FALSE;
	worker->cached_plugin = NULL;

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

void plugin_setShadowContext(Plugin* plugin, gboolean isShadowContext) {
	MAGIC_ASSERT(plugin);
	plugin->isShadowContext = isShadowContext;
}

void plugin_registerResidentState(Plugin* plugin, PluginFunctionTable* callbackFunctions, guint nVariables, va_list variableArguments) {
	MAGIC_ASSERT(plugin);
	if(plugin->isRegisterred) {
		warning("ignoring duplicate state registration");
		return;
	}

	/* these are the physical memory addresses and sizes for each variable */
	plugin->residentState = pluginstate_new(callbackFunctions, nVariables, variableArguments);

	/* also store a copy of the defaults as they exist now */
	plugin->defaultState = pluginstate_copyNew(plugin->residentState);

	/* dont change our resident state or defaults */
	plugin->isRegisterred = TRUE;
}

static void _plugin_startExecuting(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);
	g_assert(!plugin->isExecuting);

	Worker* worker = worker_getPrivate();

	/* context switch from shadow to plug-in library
	 *
	 * TODO: we can be smarter here - save a pointer to the last plugin that
	 * was loaded... if the physical memory locations still has our state,
	 * there is no need to copy it in again. similarly for stopExecuting()
	 */
	pluginstate_copy(state, plugin->residentState);
	plugin->isExecuting = TRUE;
	worker->cached_plugin = plugin;
	plugin_setShadowContext(plugin, FALSE);
}

static void _plugin_stopExecuting(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);

	Worker* worker = worker_getPrivate();

	/* context switch back to shadow from plug-in library */
	pluginstate_copy(plugin->residentState, state);
	plugin->isExecuting = FALSE;
	worker->cached_plugin = NULL;
	plugin_setShadowContext(plugin, TRUE);
}

void plugin_executeNew(Plugin* plugin, PluginState* state, gint argcParam, gchar* argvParam[]) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->new(argcParam, argvParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeFree(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->free();
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeReadable(Plugin* plugin, PluginState* state, gint socketParam) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->readable(socketParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeWritable(Plugin* plugin, PluginState* state, gint socketParam) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->writable(socketParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeWritableReadable(Plugin* plugin, PluginState* state, gint socketParam) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->writable(socketParam);
	plugin->residentState->functions->readable(socketParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeReadableWritable(Plugin* plugin, PluginState* state, gint socketParam) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->residentState->functions->readable(socketParam);
	plugin->residentState->functions->writable(socketParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeGeneric(Plugin* plugin, PluginState* state, CallbackFunc callback, gpointer data, gpointer callbackArgument) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	callback(data, callbackArgument);
	_plugin_stopExecuting(plugin, state);
}
