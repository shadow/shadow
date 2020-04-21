/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <stddef.h>

#include "async_priority_queue.h"
#include "priority_queue.h"
#include "utility.h"

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
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    priorityqueue_clear(q->pq);
    g_mutex_unlock(&(q->lock));
}

void asyncpriorityqueue_free(AsyncPriorityQueue *q) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    priorityqueue_free(q->pq);
    q->pq = NULL;
    g_mutex_unlock(&(q->lock));
    g_mutex_clear(&(q->lock));
    g_free(q);
}

gsize asyncpriorityqueue_getLength(AsyncPriorityQueue *q) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gsize returnVal = priorityqueue_getLength(q->pq);
    g_mutex_unlock(&(q->lock));
    return returnVal;
}

gboolean asyncpriorityqueue_isEmpty(AsyncPriorityQueue *q) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gboolean returnVal = priorityqueue_isEmpty(q->pq);
    g_mutex_unlock(&(q->lock));
    return returnVal;
}

gboolean asyncpriorityqueue_push(AsyncPriorityQueue *q, gpointer data) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gboolean returnVal = priorityqueue_push(q->pq, data);
    g_mutex_unlock(&(q->lock));
    return returnVal;
}

gpointer asyncpriorityqueue_peek(AsyncPriorityQueue *q) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gpointer returnData = priorityqueue_peek(q->pq);
    g_mutex_unlock(&(q->lock));
    return returnData;
}

gpointer asyncpriorityqueue_find(AsyncPriorityQueue *q, gpointer data) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gpointer returnData = priorityqueue_find(q->pq, data);
    g_mutex_unlock(&(q->lock));
    return returnData;
}

gpointer asyncpriorityqueue_pop(AsyncPriorityQueue *q) {
    utility_assert(q);
    g_mutex_lock(&(q->lock));
    gpointer returnData = priorityqueue_pop(q->pq);
    g_mutex_unlock(&(q->lock));
    return returnData;
}
