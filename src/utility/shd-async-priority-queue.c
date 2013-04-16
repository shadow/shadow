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

#include <glib.h>

#include "shd-async-priority-queue.h"
#include "shd-priority-queue.h"

struct _AsyncPriorityQueue {
	PriorityQueue* pq;
	GMutex lock;
};

AsyncPriorityQueue* asyncpriorityqueue_new(GCompareDataFunc compareFunc,
		gpointer compareData, GDestroyNotify freeFunc) {
	AsyncPriorityQueue* q = g_new0(AsyncPriorityQueue, 1);
	q->pq = priorityqueue_new(compareFunc, compareData, freeFunc);
	g_mutex_init(&(q->lock));
	return q;
}

void asyncpriorityqueue_clear(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	priorityqueue_clear(q->pq);
	g_mutex_unlock(&(q->lock));
}

void asyncpriorityqueue_free(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	priorityqueue_free(q->pq);
	q->pq = NULL;
	g_mutex_unlock(&(q->lock));
	g_mutex_clear(&(q->lock));
	g_free(q);
}

gsize asyncpriorityqueue_getLength(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gsize returnVal = priorityqueue_getLength(q->pq);
	g_mutex_unlock(&(q->lock));
	return returnVal;
}

gboolean asyncpriorityqueue_isEmpty(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gboolean returnVal = priorityqueue_isEmpty(q->pq);
	g_mutex_unlock(&(q->lock));
	return returnVal;
}

gboolean asyncpriorityqueue_push(AsyncPriorityQueue *q, gpointer data) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gboolean returnVal = priorityqueue_push(q->pq, data);
	g_mutex_unlock(&(q->lock));
	return returnVal;
}

gpointer asyncpriorityqueue_peek(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gpointer returnData = priorityqueue_peek(q->pq);
	g_mutex_unlock(&(q->lock));
	return returnData;
}

gpointer asyncpriorityqueue_find(AsyncPriorityQueue *q, gpointer data) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gpointer returnData = priorityqueue_find(q->pq, data);
	g_mutex_unlock(&(q->lock));
	return returnData;
}

gpointer asyncpriorityqueue_pop(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(&(q->lock));
	gpointer returnData = priorityqueue_pop(q->pq);
	g_mutex_unlock(&(q->lock));
	return returnData;
}
