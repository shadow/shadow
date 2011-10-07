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

RunnableVTable generatecdf_vtable = {
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

	action_init(&(action->super), &generatecdf_vtable);

	action->name = g_string_new(name->str);
	action->center = center;
	action->width = width;
	action->tail = tail;

	return action;
}

void generatecdf_run(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

//				/* normally this would happen at the event exe time */
//				cdf_tp cdf = cdf_generate(op->base_delay, op->base_width, op->tail_width);
//				if(cdf != NULL) {
//					g_hash_table_insert(wo->loaded_cdfs, gint_key(op->id), cdf);
//				}
}

void generatecdf_free(GenerateCDFAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->name, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
