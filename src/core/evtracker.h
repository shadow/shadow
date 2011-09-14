/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#ifndef _evtracker_h
#define _evtracker_h

#include <glib.h>
#include "global.h"
#include "heap.h"
#include "log.h"

#define EVTRACKER_HEAP_DEAFAULTSIZE 256
#define EVTRACKER_DATASTORE_DEFAULTSIZE 1

struct EVTRACKER_HASH_E {
	struct EVTRACKER_HASH_E * p;
	struct EVTRACKER_HASH_E * l;
	struct EVTRACKER_HASH_E * r;
	gpointer * data;
	guint data_size;
	guint data_write_ptr;
	guint data_read_ptr;
	ptime_t time;
};

struct EVTRACKER_HEAP_E {
	ptime_t time;
	struct EVTRACKER_HASH_E * hash_e;
	guint hash_offset;
};

typedef struct EVTRACKER {
	struct EVTRACKER_HASH_E ** evhash;
	heap_tp evheap;
	guint size;
	guint granularity;
	guint num_events;

	ptime_t last_accessed_time;
	struct EVTRACKER_HASH_E * last_hash_e;
} * evtracker_tp;


/**
 * Returns the next soonest event for the given evtracker.
 *
 * @param time Output - will contain the time this event expires (if provided, is optional)
 * @param removal If set nonzero, will remove the event from the evtracker
 */
gpointer evtracker_get_nextevent(evtracker_tp evt, ptime_t * time, gchar removal);

/**
 * Returns the total number of events held within the Evt
 */
guint evtracker_get_numevents(evtracker_tp evt);

/**
 * Returns nonzero if the next soonest time is the ptime_t parameter given.
 * @param time The time to check against the evtracker
 */
gint evtracker_isnext(evtracker_tp evt, ptime_t time);

/**
 * Inserts an event ginto the evtracker.
 * @param time Time event occurs
 * @param data Data associated with event
 */
void evtracker_insert_event(evtracker_tp evt, ptime_t time, gpointer data);

/**
 * Destroys the given evtracker
 */
void evtracker_destroy(evtracker_tp);

/**
 * Creates an evtracker
 *
 * @param buf_size The size of memory to allocate for the quick-insertion hashtable.
 * @param granularity All events inserted will be accessible only within windows of granularity of the specified size
 */
evtracker_tp evtracker_create(size_t buf_size, guint granularity);

struct EVTRACKER_HASH_E * evtracker_find_hash_e(evtracker_tp evt, ptime_t time, gchar create_okay);
gint evtracker_heap_e_compare(gpointer a, gpointer b);

/**
 * @param maximum If a pointer is passed, this function will never return
 *                a value greater than this.
 */
ptime_t evtracker_earliest_event(evtracker_tp evt, ptime_t * maximum);

#endif
