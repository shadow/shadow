/**
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

RunnableVTable loadcdf_vtable = {
	(RunnableRunFunc) loadcdf_run,
	(RunnableFreeFunc) loadcdf_free,
	MAGIC_VALUE
};

LoadCDFAction* loadcdf_new(GString* name, GString* path) {
	g_assert(name && path);
	LoadCDFAction* action = g_new0(LoadCDFAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadcdf_vtable);

	action->name = g_string_new(name->str);
	action->path = g_string_new(path->str);

	return action;
}

void loadcdf_run(LoadCDFAction* action) {
	MAGIC_ASSERT(action);

//	cdf_tp cdf = cdf_create(op->filepath);
//	if(cdf != NULL) {
//		g_hash_table_insert(wo->loaded_cdfs, gint_key(op->id), cdf);
//	}
}

void loadcdf_free(LoadCDFAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->name, TRUE);
	g_string_free(action->path, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
