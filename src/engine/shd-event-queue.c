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
#ifdef _EVENTQUEUESIMPLE_
	AsyncPriorityQueue* events;
#else
	/* this tree holds async event queues that themselves contain this node's
	 * events. the node has an event queue for each time interval in the
	 * simulation for which it currently has an event. queues for intervals
	 * are created on the fly, and so there are no queues for intervals that
	 * dont yet have events.
	 */
	AsyncTree* intervalEventQueues;

	/* a simple priority queue holding events in the current interval.
	 * events in this queue should not be modified by other nodes. */
	AsyncPriorityQueue* currentEventQueue;
#endif

	struct {
		guint totalIntervals;
		guint totalIntervalsWithEvents;
		guint totalEventsInDensestInterval;
	} stats;

	MAGIC_DECLARE;
};

#ifndef _EVENTQUEUESIMPLE_
static gint _eventqueue_compare_keys(const SimulationTime* a,
		const SimulationTime* b, gpointer user_data) {
	g_assert(a);
	g_assert(b);
	return *a > *b ? +1 : *a == *b ? 0 : -1;
}
#endif

EventQueue* eventqueue_new() {
	EventQueue* eventq = g_new0(EventQueue, 1);
	MAGIC_INIT(eventq);

#ifdef _EVENTQUEUESIMPLE_
	eventq->events = asyncpriorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL, (GDestroyNotify)shadowevent_free);
#else
	eventq->intervalEventQueues = asynctree_new_full(
			(GCompareDataFunc)_eventqueue_compare_keys, NULL, g_free,
			(GDestroyNotify)asyncpriorityqueue_free);
	eventq->currentEventQueue = NULL;
#endif

	return eventq;
}

void eventqueue_free(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);

#ifdef _EVENTQUEUESIMPLE_
	asyncpriorityqueue_free(eventq->events);
#else
	asynctree_unref(eventq->intervalEventQueues);
	if(eventq->currentEventQueue) {
		asyncpriorityqueue_free(eventq->currentEventQueue);
	}
#endif

	message("%u/%u intervals had events, at most %u",
			eventq->stats.totalIntervalsWithEvents, eventq->stats.totalIntervals,
			eventq->stats.totalEventsInDensestInterval);

	MAGIC_CLEAR(eventq);
	g_free(eventq);
}

void eventqueue_push(EventQueue* eventq, Event* event, SimulationTime intervalNumber) {
	MAGIC_ASSERT(eventq);

#ifdef _EVENTQUEUESIMPLE_
	asyncpriorityqueue_push(eventq->events, event);
#else
	AsyncPriorityQueue* apq = (AsyncPriorityQueue*) asynctree_lookup(
			eventq->intervalEventQueues, (gconstpointer) &intervalNumber);

	if(apq == NULL) {
		apq = asyncpriorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL, (GDestroyNotify)shadowevent_free);
		SimulationTime* n = g_new0(SimulationTime, 1);
		*n = intervalNumber;
		asynctree_insert(eventq->intervalEventQueues, n, apq);
	}

	asyncpriorityqueue_push(apq, event);
#endif
}

void eventqueue_startInterval(EventQueue* eventq, SimulationTime intervalNumber) {
	MAGIC_ASSERT(eventq);
#ifdef _EVENTQUEUESIMPLE_
	return;
#else
	g_assert(eventq->currentEventQueue == NULL);

	eventq->currentEventQueue = (AsyncPriorityQueue*) asynctree_lookup(
			eventq->intervalEventQueues, (gconstpointer) &intervalNumber);

	/* processing stats */
	eventq->stats.totalIntervals++;
	gsize n = 0;

	if(eventq->currentEventQueue) {
		n = asyncpriorityqueue_getLength(eventq->currentEventQueue);
		if(n > 0) {
			eventq->stats.totalIntervalsWithEvents++;
			if(n > eventq->stats.totalEventsInDensestInterval) {
				eventq->stats.totalEventsInDensestInterval = n;
			}
		}
	}
#endif
}

Event* eventqueue_pop(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);
#ifdef _EVENTQUEUESIMPLE_
	return (Event*) asyncpriorityqueue_pop(eventq->events);
#else
	return eventq->currentEventQueue ?
			asyncpriorityqueue_pop(eventq->currentEventQueue) : NULL;
#endif
}

#ifndef _EVENTQUEUESIMPLE_
static gboolean _peek_tree(SimulationTime* key, AsyncPriorityQueue* value, AsyncPriorityQueue** user_data) {
	if(value && !asyncpriorityqueue_isEmpty(value)) {
		*user_data = value;
		return TRUE;
	} else {
		return FALSE;
	}
}
#endif

Event* eventqueue_peek(EventQueue* eventq) {
#ifdef _EVENTQUEUESIMPLE_
	return (Event*) asyncpriorityqueue_peek(eventq->events);
#else
	if(eventq->currentEventQueue && !asyncpriorityqueue_isEmpty(eventq->currentEventQueue)) {
		return asyncpriorityqueue_peek(eventq->currentEventQueue);
	} else {
		AsyncPriorityQueue* nextq = NULL;
		/* this traverses in order, so the first callback is the min key */
		asynctree_foreach(eventq->intervalEventQueues,
				(GTraverseFunc)_peek_tree, &nextq);
		g_assert(nextq);
		return asyncpriorityqueue_peek(nextq);
	}
#endif
}

void eventqueue_endInterval(EventQueue* eventq, SimulationTime intervalNumber) {
	MAGIC_ASSERT(eventq);
#ifdef _EVENTQUEUESIMPLE_
	return;
#else
	if(eventq->currentEventQueue != NULL) {
		asynctree_remove(eventq->intervalEventQueues, (gconstpointer) &intervalNumber);
		eventq->currentEventQueue = NULL;
	}
#endif
}
