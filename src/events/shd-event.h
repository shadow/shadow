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

#ifndef SHD_EVENT_H_
#define SHD_EVENT_H_

#include <glib.h>

typedef struct _Event Event;
typedef struct _EventVTable EventVTable;

/* required functions */
typedef void (*EventExecuteFunc)(Event* event);
typedef void (*EventFreeFunc)(Event* event);

/*
 * Virtual function table for base event, storing pointers to required
 * callable functions.
 */
struct _EventVTable {
	EventExecuteFunc execute;
	EventFreeFunc free;

	MAGIC_DECLARE;
};

/*
 * A base event and its members. Subclasses extend Event by keeping this as
 * the first element in the substructure, and adding custom members below it.
 */
struct _Event {
	EventVTable* vtable;
	SimulationTime time;

	MAGIC_DECLARE;
};

void event_init(Event* event, EventVTable* vtable);
void event_execute(Event* event);
gint event_compare(gconstpointer eventa, gconstpointer eventb, gpointer user_data);
void event_free(gpointer data);

#endif /* SHD_EVENT_H_ */
