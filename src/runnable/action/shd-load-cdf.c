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
