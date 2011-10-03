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

void event_init(Event* event, EventVTable* vtable) {
	g_assert(event && vtable);
	MAGIC_INIT(event);
	MAGIC_INIT(vtable);

	event->vtable = vtable;
	event->time = 0;
}

void event_execute(Event* event) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);

	event->vtable->execute(event);
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

	MAGIC_CLEAR(event);
	event->vtable->free(event);
}
