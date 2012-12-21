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

	PluginNewInstanceFunc new;
	PluginNotifyFunc free;
	PluginNotifyFunc notify;

	gsize residentStateSize;
	gpointer residentStatePointer;
	gpointer residentState;
	PluginState defaultState;

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
	 *
	 * @note this will call g_module_check_init() in the plug-in if it contains
	 * that function.
	 */
	plugin->handle = g_module_open(plugin->path->str, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
	if(plugin->handle) {
		message("successfully loaded private plug-in '%s' at %p", plugin->path->str, plugin);
	} else {
		const gchar* errorMessage = g_module_error();
		critical("g_module_open() failed: %s", errorMessage);
		error("unable to load private plug-in '%s'", plugin->path->str);
	}

	/* make sure it has the required init function */
	gpointer initFunc = NULL;
	gpointer hoistedGlobals = NULL;
	gpointer hoistedGlobalsSize = NULL;
	gpointer hoistedGlobalsPointer = NULL;
	gboolean success = FALSE;

	success = g_module_symbol(plugin->handle, PLUGININITSYMBOL, &initFunc);
	if(success) {
		plugin->init = initFunc;
		message("found '%s' at %p", PLUGININITSYMBOL, initFunc);
	} else {
		const gchar* errorMessage = g_module_error();
		critical("g_module_symbol() failed: %s", errorMessage);
		error("unable to find the required function symbol '%s' in plug-in '%s'",
				PLUGININITSYMBOL, filename->str);
	}

	success = g_module_symbol(plugin->handle, PLUGINGLOBALSSYMBOL, &hoistedGlobals);
	if(success) {
		plugin->residentState = hoistedGlobals;
		message("found '%s' at %p", PLUGINGLOBALSSYMBOL, hoistedGlobals);
	} else {
		const gchar* errorMessage = g_module_error();
		critical("g_module_symbol() failed: %s", errorMessage);
		error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
				PLUGINGLOBALSSYMBOL, filename->str);
	}

	success = g_module_symbol(plugin->handle, PLUGINGLOBALSPOINTERSYMBOL, &hoistedGlobalsPointer);
	if(success) {
		plugin->residentStatePointer = hoistedGlobalsPointer;
		message("found '%s' at %p", PLUGINGLOBALSPOINTERSYMBOL, hoistedGlobalsPointer);
	} else {
		const gchar* errorMessage = g_module_error();
		critical("g_module_symbol() failed: %s", errorMessage);
		error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
				PLUGINGLOBALSPOINTERSYMBOL, filename->str);
	}

	success = g_module_symbol(plugin->handle, PLUGINGLOBALSSIZESYMBOL, &hoistedGlobalsSize);
	if(success) {
		g_assert(hoistedGlobalsSize);
		gint s = *((gint*) hoistedGlobalsSize);
		plugin->residentStateSize = (gsize) s;
		message("found '%s' of value '%i' at %p", PLUGINGLOBALSSIZESYMBOL, s, hoistedGlobalsSize);
	} else {
		const gchar* errorMessage = g_module_error();
		critical("g_module_symbol() failed: %s", errorMessage);
		error("unable to find the required merged globals struct symbol '%s' in plug-in '%s'",
				PLUGINGLOBALSSIZESYMBOL, filename->str);
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

	if(!(plugin->isRegisterred)) {
		error("The plug-in '%s' must call shadowlib_register()", plugin->path->str);
	}

	return plugin;
}

void plugin_free(gpointer data) {
	Plugin* plugin = data;
	MAGIC_ASSERT(plugin);

	if(plugin->handle) {
		gboolean success = g_module_close(plugin->handle);
		if(!success) {
			const gchar* errorMessage = g_module_error();
			warning("g_module_close() failed: %s", errorMessage);
			warning("failed closing plugin '%s'", plugin->path->str);
		}
	}

	g_unlink(plugin->path->str);
	g_string_free(plugin->path, TRUE);

	if(plugin->defaultState) {
		plugin_freeState(plugin, plugin->defaultState);
	}

	MAGIC_CLEAR(plugin);
	g_free(plugin);
}

void plugin_setShadowContext(Plugin* plugin, gboolean isShadowContext) {
	MAGIC_ASSERT(plugin);
	plugin->isShadowContext = isShadowContext;
}

void plugin_registerResidentState(Plugin* plugin, PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify) {
	MAGIC_ASSERT(plugin);
	if(plugin->isRegisterred) {
		warning("ignoring duplicate state registration");
		return;
	}

	g_assert(new && free && notify);

	/* store the pointers to the callbacks the plugin wants us to call */
	plugin->new = new;
	plugin->free = free;
	plugin->notify = notify;

	/* also store a copy of the defaults as they exist now */
	debug("copying resident plugin memory contents at %p-%p (%lu bytes) as default start state",
			plugin->residentState, plugin->residentState+plugin->residentStateSize, plugin->residentStateSize);
	plugin->defaultState = g_slice_copy(plugin->residentStateSize, plugin->residentState);
	debug("stored default state at %p", plugin->defaultState);

	/* dont change our resident state or defaults */
	plugin->isRegisterred = TRUE;
}

static void _plugin_startExecuting(Plugin* plugin, PluginState state) {
	MAGIC_ASSERT(plugin);
	g_assert(!plugin->isExecuting);

	Worker* worker = worker_getPrivate();

	/* context switch from shadow to plug-in library
	 *
	 * TODO: we can be smarter here - save a pointer to the last plugin that
	 * was loaded... if the physical memory locations still has our state,
	 * there is no need to copy it in again. similarly for stopExecuting()
	 */
	/* destination, source, size */
	g_memmove(plugin->residentState, state, plugin->residentStateSize);

	plugin->isExecuting = TRUE;
	worker->cached_plugin = plugin;
	g_timer_start(plugin->delayTimer);
	plugin_setShadowContext(plugin, FALSE);
}

static void _plugin_stopExecuting(Plugin* plugin, PluginState state) {
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

	/* destination, source, size */
	g_memmove(state, plugin->residentState, plugin->residentStateSize);
	worker->cached_plugin = NULL;
}

void plugin_executeNew(Plugin* plugin, PluginState state, gint argcParam, gchar* argvParam[]) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->new(argcParam, argvParam);
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeFree(Plugin* plugin, PluginState state) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->free();
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeNotify(Plugin* plugin, PluginState state) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	plugin->notify();
	_plugin_stopExecuting(plugin, state);
}

void plugin_executeGeneric(Plugin* plugin, PluginState state, CallbackFunc callback, gpointer data, gpointer callbackArgument) {
	MAGIC_ASSERT(plugin);
	_plugin_startExecuting(plugin, state);
	callback(data, callbackArgument);
	_plugin_stopExecuting(plugin, state);
}

PluginState plugin_newDefaultState(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return g_slice_copy(plugin->residentStateSize, plugin->defaultState);
}

void plugin_freeState(Plugin* plugin, gpointer state) {
	MAGIC_ASSERT(plugin);
	g_slice_free1(plugin->residentStateSize, state);
}

GQuark* plugin_getID(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return &(plugin->id);
}

gboolean plugin_isShadowContext(Plugin* plugin) {
	MAGIC_ASSERT(plugin);
	return plugin->isShadowContext;
}
