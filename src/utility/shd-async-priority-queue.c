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

#include <glib.h>

#include "shd-async-priority-queue.h"
#include "shd-priority-queue.h"

struct _AsyncPriorityQueue {
	PriorityQueue* pq;
	GMutex* lock;
};

AsyncPriorityQueue* asyncpriorityqueue_new(GCompareDataFunc compareFunc,
		gpointer compareData, GDestroyNotify freeFunc) {
	AsyncPriorityQueue* q = g_new0(AsyncPriorityQueue, 1);
	q->pq = priorityqueue_new(compareFunc, compareData, freeFunc);
	q->lock = g_mutex_new();
	return q;
}

void asyncpriorityqueue_clear(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	priorityqueue_clear(q->pq);
	g_mutex_unlock(q->lock);
}

void asyncpriorityqueue_free(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	priorityqueue_free(q->pq);
	q->pq = NULL;
	g_mutex_unlock(q->lock);
	g_mutex_free(q->lock);
	g_free(q);
}

gsize asyncpriorityqueue_getLength(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gsize returnVal = priorityqueue_getLength(q->pq);
	g_mutex_unlock(q->lock);
	return returnVal;
}

gboolean asyncpriorityqueue_isEmpty(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gboolean returnVal = priorityqueue_isEmpty(q->pq);
	g_mutex_unlock(q->lock);
	return returnVal;
}

gboolean asyncpriorityqueue_push(AsyncPriorityQueue *q, gpointer data) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gboolean returnVal = priorityqueue_push(q->pq, data);
	g_mutex_unlock(q->lock);
	return returnVal;
}

gpointer asyncpriorityqueue_peek(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gpointer returnData = priorityqueue_peek(q->pq);
	g_mutex_unlock(q->lock);
	return returnData;
}

gpointer asyncpriorityqueue_find(AsyncPriorityQueue *q, gpointer data) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gpointer returnData = priorityqueue_find(q->pq, data);
	g_mutex_unlock(q->lock);
	return returnData;
}

gpointer asyncpriorityqueue_pop(AsyncPriorityQueue *q) {
	g_assert(q);
	g_mutex_lock(q->lock);
	gpointer returnData = priorityqueue_pop(q->pq);
	g_mutex_unlock(q->lock);
	return returnData;
}
