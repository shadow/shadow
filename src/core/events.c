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

#include <glib.h>
#include <stdlib.h>

#include "global.h"
#include "events.h"
#include "sysconfig.h"
#include "timer.h"
#include "sim.h"
#include "vci.h"

events_tp events_create (void) {
	events_tp events;

	events = malloc(sizeof(*events));
	if(!events)
		printfault(EXIT_NOMEM, "events_create: Out of memory");

	events->evtracker = evtracker_create(sysconfig_get_gint("event_tracker_size"), sysconfig_get_gint("event_tracker_granularity"));

	return events;
}

void events_schedule (events_tp events, ptime_t at, gpointer data, gint type) {
	events_eholder_tp eh = malloc(sizeof(*eh));

	if(!eh)
		printfault(EXIT_NOMEM, "events_schedule: Out of memory");

	eh->d = data;
	eh->type = type;

	evtracker_insert_event(events->evtracker, at, eh);

	return;
}

ptime_t events_get_next_time (events_tp events) {
	return evtracker_earliest_event(events->evtracker, NULL);
}

gpointer events_dequeue  (events_tp events, ptime_t * at, gint * type) {
	events_eholder_tp eh;
	gpointer rv;

	eh = evtracker_get_nextevent(events->evtracker, at, 1);
	if(!eh)
		return NULL;

	rv = eh->d;
	if(type)
		*type = eh->type;

	free(eh);
	return rv;
}

void events_destroy (events_tp events) {
	if(events == NULL || events->evtracker == NULL) {
		return;
	}

	gpointer event;
	gint event_type = 0;

	/* empty the event queue, destroying each */
	while((event = events_dequeue(events, NULL, &event_type)) != NULL) {
		/* different events have different requirements for deletion */
		switch(event_type) {
		case EVENTS_TYPE_DTIMER:
			dtimer_destroy_event(event);
			break;

		case EVENTS_TYPE_VCI:
			vci_destroy_event(NULL, event);
			break;

		case EVENTS_TYPE_SIMOP:
			simop_destroy(event);
			break;

		case EVENTS_TYPE_TICKTOCK:
			/* empty event */
			break;

		default:
			free(event);
		}
	}

	evtracker_destroy(events->evtracker);
	free(events);
}



