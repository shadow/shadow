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
