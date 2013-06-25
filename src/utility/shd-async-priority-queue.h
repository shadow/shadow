/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ASYNC_PRIORITY_QUEUE_H_
#define SHD_ASYNC_PRIORITY_QUEUE_H_

typedef struct _AsyncPriorityQueue AsyncPriorityQueue;

AsyncPriorityQueue* asyncpriorityqueue_new(GCompareDataFunc compareFunc,
		gpointer compareData, GDestroyNotify freeFunc);
void asyncpriorityqueue_clear(AsyncPriorityQueue *q);
void asyncpriorityqueue_free(AsyncPriorityQueue *q);

gsize asyncpriorityqueue_getLength(AsyncPriorityQueue *q);
gboolean asyncpriorityqueue_isEmpty(AsyncPriorityQueue *q);
gboolean asyncpriorityqueue_push(AsyncPriorityQueue *q, gpointer data);
gpointer asyncpriorityqueue_peek(AsyncPriorityQueue *q);
gpointer asyncpriorityqueue_find(AsyncPriorityQueue *q, gpointer data);
gpointer asyncpriorityqueue_pop(AsyncPriorityQueue *q);

#endif /* SHD_ASYNC_PRIORITY_QUEUE_H_ */
