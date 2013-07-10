/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _EventQueue {
	GMutex lock;
	PriorityQueue* pq;
	gsize nPushed;
	gsize nPopped;
	SimulationTime sequenceCounter;

	MAGIC_DECLARE;
};

EventQueue* eventqueue_new() {
	EventQueue* eventq = g_new0(EventQueue, 1);
	MAGIC_INIT(eventq);

	eventq->pq = priorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL, (GDestroyNotify)shadowevent_free);
	g_mutex_init(&(eventq->lock));

	eventq->nPushed = eventq->nPopped = 0;

	return eventq;
}

void eventqueue_free(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);

	g_mutex_lock(&(eventq->lock));
	priorityqueue_free(eventq->pq);
	eventq->pq = NULL;
	g_mutex_unlock(&(eventq->lock));
	g_mutex_clear(&(eventq->lock));

	MAGIC_CLEAR(eventq);
	g_free(eventq);
}

void eventqueue_push(EventQueue* eventq, Event* event) {
	MAGIC_ASSERT(eventq);
	if(event) {
		g_mutex_lock(&(eventq->lock));
		event->sequence = ++(eventq->sequenceCounter);
		priorityqueue_push(eventq->pq, event);
		(eventq->nPushed)++;
		g_mutex_unlock(&(eventq->lock));
	}

}

Event* eventqueue_pop(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);
	g_mutex_lock(&(eventq->lock));
	Event* retEvent = priorityqueue_pop(eventq->pq);
	if(retEvent) {
		(eventq->nPopped)++;
	}
	g_mutex_unlock(&(eventq->lock));
	return retEvent;
}

Event* eventqueue_peek(EventQueue* eventq) {
	MAGIC_ASSERT(eventq);
	g_mutex_lock(&(eventq->lock));
	Event* retEvent = priorityqueue_peek(eventq->pq);
	g_mutex_unlock(&(eventq->lock));
	return retEvent;
}
