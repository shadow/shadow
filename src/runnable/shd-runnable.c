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

void runnable_init(Runnable* r, RunnableVTable* vtable) {
	g_assert(r && vtable);
	MAGIC_INIT(r);
	MAGIC_INIT(vtable);
	r->vtable = vtable;
}

void runnable_run(gpointer data) {
	Runnable* r = data;
	MAGIC_ASSERT(r);
	MAGIC_ASSERT(r->vtable);
	r->vtable->run(r);
}

void runnable_free(gpointer data) {
	Runnable* r = data;
	MAGIC_ASSERT(r);
	MAGIC_ASSERT(r->vtable);
	MAGIC_CLEAR(r);
	r->vtable->free(r);
}
