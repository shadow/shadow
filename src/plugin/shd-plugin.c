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

#include <unistd.h>
#include <glib/gstdio.h>

#include "shadow.h"

struct _Plugin {
	GQuark id;
	GString* path;
	GModule* handle;
	GTimer* delayTimer;

	ShadowPluginInitializeFunc init;
	PluginFunctionTable* callbackFunctions;

	PluginState* residentState;
	PluginState* defaultState;
	gboolean isRegisterred;
	/*
	 * TRUE from when we've called into plug-in code until the call completes.
	 * Note that the plug-in may get back into shadow code during execution, by
	 * calling one of the shadowlib functions or calling a function that we
	 * intercept. isShadowContext distinguishes this.
	 */
	gboolean isExecuting;
	/*
	 * Distinguishes which context we are in. Whenever the flow of execution
	 * passes into the plug-in, this is FALSE, and whenever it comes back to
	 * shadow, this is TRUE. This is used to determine if we should actually
	 * be intercepting functions or not, since we dont want to intercept them
	 * if they provide shadow with needed functionality.
	 *
	 * We must be careful to set this correctly at every boundry (shadowlib,
	 * interceptions, etc)
	 */
	gboolean isShadowContext;
	MAGIC_DECLARE;
};

GString* _plugin_getTemporaryFilePath(gchar* originalPath) {
	/* get the basename of the real plug-in and create a temp file template */
	gchar* basename = g_path_get_basename(originalPath);
	GString* templateBuffer = g_string_new(basename);
	g_free(basename);

	templateBuffer = g_string_prepend(templateBuffer, "XXXXXX-");
	gchar* template = g_string_free(templateBuffer, FALSE);

	/* try to open the templated file, checking for errors */
	gchar* temporaryFilename = NULL;
	GError* error = NULL;
	gint openedFile = g_file_open_tmp((const gchar*) template, &temporaryFilename, &error);
	if(openedFile < 0) {
		error("unable to open temporary file for plug-in '%s': %s", originalPath, error->message);
	}

	/* now we ceanup and return the new filename */
	close(openedFile);
	g_free(template);

	GString* templatePath = g_string_new(temporaryFilename);
	g_free(temporaryFilename);
	return templatePath;
}

gboolean _plugin_copyFile(gchar* fromPath, gchar* toPath) {
	gchar* contents = NULL;
	gsize length = 0;
	GError* error = NULL;

	/* get the original file */
	if(!g_file_get_contents(fromPath, &contents, &length, &error)) {
		error("unable to read '%s' for copying: %s", fromPath, error->message);
		return FALSE;
	}
	error = NULL;

	/* copy to the new file */
	if(!g_file_set_contents(toPath, contents, (gssize)length, &error)) {
		error("unable to write private copy of '%s' to '%s': %s",
				fromPath, toPath, error->message);
		return FALSE;
	}

	/* ok, our private copy was created, cleanup */
	g_free(contents);
	return TRUE;
}

Plugin* plugin_new(GQuark id, GString* filename) {
	g_assert(filename);
	Plugin* plugin = g_new0(Plugin, 1);
	MAGIC_INIT(plugin);

	plugin->id = id;

	/* timer for CPU delay measurements */
	plugin->delayTimer = g_timer_new();

	/* do not open the path directly, but rather copy to tmp directory first
	 * to avoid multiple threads using the same memory space.
	 */
	plugin->path = _plugin_getTemporaryFilePath(filename->str);

	/* now we need to copy the actual contents to our new file */
	if(!_plugin_copyFile(filename->str, plugin->path->str)) {
		g_string_free(plugin->path, TRUE);
		g_free(plugin);
		return NULL;
	}

	/*
	 * now get the plugin handle from our private copy of the library.
	 *
	 * @warning only global dlopens are searchable with dlsym
	 * we cant use G_MODULE_BIND_LOCAL if we want to be able to lookup
	 * functions using dlsym in the plugin itself. if G_MODULE_BIND_LOCAL
	 * functionality is desired, then we must require plugins to separate their
	 * intercepted functions to a SHARED library, and link the plugin to that.
	 */
	plugin->handle = g_module_open(plugin->path->str, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
	if(plugin->handle) {
		message("successfully loaded private plug-in '%s' at %p", plugin->path->str, plugin);
	} else {
		error("unable to load private plug-in '%s'", plugin->path->str);
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

	/* we will store the callback functions the plug-in tells us */
	plugin->callbackFunctions = g_new0(PluginFunctionTable, 1);

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
			warning("failed closing plugin '%s'", plugin->path->str);
		}
	}

	g_unlink(plugin->path->str);
	g_string_free(plugin->path, TRUE);

	g_free(plugin->callbackFunctions);

	if(plugin->residentState) {
		pluginstate_free(plugin->residentState);
	}
	if(plugin->defaultState) {
		pluginstate_free(plugin->defaultState);
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

	g_assert(callbackFunctions);

	/* store the pointers to the callbacks the plugin wants us to call */
	*(plugin->callbackFunctions) = *callbackFunctions;

	/* these are the physical memory addresses and sizes for each variable */
	debug("registering resident plugin memory locations");
	plugin->residentState = pluginstate_new(nVariables, variableArguments);

	/* also store a copy of the defaults as they exist now */
	debug("copying resident plugin memory location contents as default start state");
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
	g_timer_start(plugin->delayTimer);
	plugin_setShadowContext(plugin, FALSE);
}

static void _plugin_stopExecuting(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);

	Worker* worker = worker_getPrivate();

	/* context switch back to shadow from plug-in library */
	plugin_setShadowContext(plugin, TRUE);
	plugin->isExecuting = FALSE;
	/* no need to call stop */
	gdouble elapsed = g_timer_elapsed(plugin->delayTimer, NULL);

	SimulationTime delay = (SimulationTime) (elapsed * SIMTIME_ONE_SECOND);
	cpu_addDelay(node_getCPU(worker->cached_node), delay);
	tracker_addProcessingTime(node_getTracker(worker->cached_node), delay);

	pluginstate_copy(plugin->residentState, state);
	worker->cached_plugin = NULL;
}

void plugin_executeNew(Plugin* plugin, PluginState* state, gint argcParam, gchar* argvParam[]) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->callbackFunctions->new(argcParam, argvParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeFree(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->callbackFunctions->free();
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeNotify(Plugin* plugin, PluginState* state) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->callbackFunctions->notify();
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeGeneric(Plugin* plugin, PluginState* state, CallbackFunc callback, gpointer data, gpointer callbackArgument) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	callback(data, callbackArgument);
	_plugin_stopExecuting(plugin, state);
}

PluginState* plugin_newDefaultState(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return pluginstate_copyNew(plugin->defaultState);
}

GQuark* plugin_getID(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return &(plugin->id);
}

gboolean plugin_isShadowContext(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return plugin->isShadowContext;
}
