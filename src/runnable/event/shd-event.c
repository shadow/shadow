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

RunnableVTable event_vtable = {
	(RunnableRunFunc) event_run,
	(RunnableFreeFunc) event_free,
	MAGIC_VALUE
};

void event_init(Event* event, EventVTable* vtable) {
	g_assert(event && vtable);

	runnable_init(&(event->super), &event_vtable);

	MAGIC_INIT(event);
	MAGIC_INIT(vtable);

	event->vtable = vtable;
}

void event_run(gpointer data) {
	Event* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	event->vtable->run(event, event->node);
}

gint event_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Event* ea = a;
	const Event* eb = b;
	MAGIC_ASSERT(ea);
	MAGIC_ASSERT(eb);
	return ea->time > eb->time ? +1 : ea->time == eb->time ? 0 : -1;
}

void event_free(gpointer data) {
	Event* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	MAGIC_CLEAR(event);
	event->vtable->free(event);
}
