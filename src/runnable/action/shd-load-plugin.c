/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

struct _LoadPluginAction {
	Action super;
	GString* name;
	GString* path;
	MAGIC_DECLARE;
};

RunnableFunctionTable loadplugin_functions = {
	(RunnableRunFunc) loadplugin_run,
	(RunnableFreeFunc) loadplugin_free,
	MAGIC_VALUE
};

LoadPluginAction* loadplugin_new(GString* name, GString* path) {
	utility_assert(name && path);
	LoadPluginAction* action = g_new0(LoadPluginAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadplugin_functions);

	action->name = g_string_new(name->str);
	action->path = g_string_new(path->str);

	return action;
}

void loadplugin_run(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	/* we need a copy of the library for every thread because each of
	 * them needs a separate instance of all the plug-in state so it doesn't
	 * overlap. We'll do this lazily while booting up applications, since that
	 * event will be run by a worker. For now, we just track the default
	 * original plug-in library, so the worker can copy it later.
	 */
	Program* prog = program_new(action->name->str, action->path->str);
	worker_storeProgram(prog);
}

void loadplugin_free(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	if(action->name) {
		g_string_free(action->name, TRUE);
	}
    if(action->path) {
        g_string_free(action->path, TRUE);
    }

	MAGIC_CLEAR(action);
	g_free(action);
}
