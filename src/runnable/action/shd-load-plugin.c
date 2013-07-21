/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

struct _LoadPluginAction {
	Action super;
	GQuark id;
	GString* path;
	MAGIC_DECLARE;
};

RunnableFunctionTable loadplugin_functions = {
	(RunnableRunFunc) loadplugin_run,
	(RunnableFreeFunc) loadplugin_free,
	MAGIC_VALUE
};

LoadPluginAction* loadplugin_new(GString* name, GString* path) {
	g_assert(name && path);
	LoadPluginAction* action = g_new0(LoadPluginAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadplugin_functions);

	action->id = g_quark_from_string((const gchar*) name->str);
	action->path = g_string_new(utility_getHomePath(path->str));

	return action;
}

void loadplugin_run(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	/* we need a copy of the library for every thread because each of
	 * them needs a separate instance of all the plug-in state so it doesn't
	 * overlap. We'll do this lazily while booting up applications, since that
	 * event will be run by a worker. For now, we just track the original
	 * filename of the plug-in library, so the worker can copy it later.
	 */
	Worker* worker = worker_getPrivate();

	/* the hash table now owns the GString */
	GQuark* id = g_new0(GQuark, 1);
	*id = action->id;

	/* make sure the path is absolute */
	if(!g_path_is_absolute(action->path->str)) {
		/* ok, we look in ~/.shadow/plugins */
		gchar* plugin = g_string_free(action->path, FALSE);
		const gchar* home = g_get_home_dir();
		gchar* path = g_build_path("/", home, ".shadow", "plugins", plugin, NULL);
		action->path = g_string_new(path);
		g_free(plugin);
		g_free(path);
	}

	engine_put(worker->cached_engine, PLUGINPATHS, id, action->path->str);
}

void loadplugin_free(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	if(action->path) {
		g_string_free(action->path, FALSE);
	}

	MAGIC_CLEAR(action);
	g_free(action);
}
