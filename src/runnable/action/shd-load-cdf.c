/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

RunnableFunctionTable loadcdf_functions = {
	(RunnableRunFunc) loadcdf_run,
	(RunnableFreeFunc) loadcdf_free,
	MAGIC_VALUE
};

LoadCDFAction* loadcdf_new(GString* name, GString* path) {
	g_assert(name && path);
	LoadCDFAction* action = g_new0(LoadCDFAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadcdf_functions);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->path = g_string_new(utility_getHomePath(path->str));

	return action;
}

void loadcdf_run(LoadCDFAction* action) {
	MAGIC_ASSERT(action);

	CumulativeDistribution* cdf = cdf_new(action->id, action->path->str);
	if(cdf) {
		Worker* worker = worker_getPrivate();
		engine_put(worker->cached_engine, CDFS, &(cdf->id), cdf);
	} else {
		critical("loading cdf '%s' from '%s' failed", g_quark_to_string(action->id), action->path->str);
	}
}

void loadcdf_free(LoadCDFAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->path, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
