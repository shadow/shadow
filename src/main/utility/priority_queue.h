/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PRIORITY_QUEUE_H
#define SHD_PRIORITY_QUEUE_H

#include <glib.h>

typedef struct _PriorityQueue PriorityQueue;

PriorityQueue* priorityqueue_new(GCompareDataFunc compareFunc, gpointer compareData,
                                 GDestroyNotify freeFunc, GHashFunc hashFunc, GEqualFunc eqFunc);
void priorityqueue_clear(PriorityQueue *q);
void priorityqueue_free(PriorityQueue *q);

gsize priorityqueue_getLength(PriorityQueue *q);
gboolean priorityqueue_isEmpty(PriorityQueue *q);
gboolean priorityqueue_push(PriorityQueue *q, gpointer data);
gpointer priorityqueue_peek(PriorityQueue *q);
gpointer priorityqueue_find(PriorityQueue *q, gpointer data);
gpointer priorityqueue_pop(PriorityQueue *q);

#endif /* SHD_PRIORITY_QUEUE_H */
