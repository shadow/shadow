/*
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

RunnableFunctionTable generatecdf_functions = {
	(RunnableRunFunc) generatecdf_run,
	(RunnableFreeFunc) generatecdf_free,
	MAGIC_VALUE
};

GenerateCDFAction* generatecdf_new(GString* name, guint64 center, guint64 width,
		guint64 tail)
{
	g_assert(name);
	GenerateCDFAction* action = g_new0(GenerateCDFAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &generatecdf_functions);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->center = center;
	action->width = width;
	action->tail = tail;

	return action;
}

void generatecdf_run(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

	CumulativeDistribution* cdf = cdf_generate(action->id, action->center, action->width, action->tail);
	if(cdf) {
		Worker* worker = worker_getPrivate();
		engine_put(worker->cached_engine, CDFS, &(cdf->id), cdf);
	} else {
		critical("generating cdf '%s' failed", g_quark_to_string(action->id));
	}
}

void generatecdf_free(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
