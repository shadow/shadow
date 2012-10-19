/**
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

struct _EventQueue {
	AsyncPriorityQueue* events;
	gsize nPushed;
	gsize nPopped;

	MAGIC_DECLARE;
};

EventQueue* eventqueue_new() {
	EventQueue* eventq = g_new0(EventQueue, 1);
	MAGIC_INIT(eventq);

	eventq->events = asyncpriorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL, (GDestroyNotify)shadowevent_free);
	eventq->nPushed = eventq->nPopped = 0;

	return eventq;
}

void eventqueue_free(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);

	asyncpriorityqueue_free(eventq->events);

	MAGIC_CLEAR(eventq);
	g_free(eventq);
}

void eventqueue_push(EventQueue* eventq, Event* event) {
	MAGIC_ASSERT(eventq);
	if(event) {
		asyncpriorityqueue_push(eventq->events, event);
		(eventq->nPushed)++;
	}
}

Event* eventqueue_pop(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);
	Event* event = (Event*) asyncpriorityqueue_pop(eventq->events);
	if(event) {
		(eventq->nPopped)++;
	}
	return event;
}

Event* eventqueue_peek(EventQueue* eventq) {
	return (Event*) asyncpriorityqueue_peek(eventq->events);
}
