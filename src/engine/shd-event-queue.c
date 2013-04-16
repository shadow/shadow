/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
