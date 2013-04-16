/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shadow.h"

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
