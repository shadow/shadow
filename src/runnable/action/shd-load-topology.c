/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

struct _LoadTopologyAction {
	Action super;
	GString* path;
	MAGIC_DECLARE;
};

RunnableFunctionTable loadtopology_functions = {
	(RunnableRunFunc) loadtopology_run,
	(RunnableFreeFunc) loadtopology_free,
	MAGIC_VALUE
};

LoadTopologyAction* loadtopology_new(GString* path) {
	g_assert(path);
	LoadTopologyAction* action = g_new0(LoadTopologyAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadtopology_functions);

	action->path = g_string_new(path->str);

	return action;
}

void loadtopology_run(LoadTopologyAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();
	Topology* topology = topology_new(action->path->str);
	if(!topology) {
		error("error loading topology file '%s'", action->path->str);
	}
//	engine_setTopology(worker->cached_engine, topology);
}

void loadtopology_free(LoadTopologyAction* action) {
	MAGIC_ASSERT(action);

	if(action->path) {
		g_string_free(action->path, FALSE);
	}

	MAGIC_CLEAR(action);
	g_free(action);
}
