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

#ifndef SHD_PRIORITY_QUEUE_H
#define SHD_PRIORITY_QUEUE_H

typedef struct _PriorityQueue PriorityQueue;

PriorityQueue* priorityqueue_new(GCompareDataFunc compareFunc,
		gpointer compareData, GDestroyNotify freeFunc);
void priorityqueue_clear(PriorityQueue *q);
void priorityqueue_free(PriorityQueue *q);

gsize priorityqueue_getLength(PriorityQueue *q);
gboolean priorityqueue_isEmpty(PriorityQueue *q);
gboolean priorityqueue_push(PriorityQueue *q, gpointer data);
gpointer priorityqueue_peek(PriorityQueue *q);
gpointer priorityqueue_find(PriorityQueue *q, gpointer data);
gpointer priorityqueue_pop(PriorityQueue *q);

#endif /* SHD_PRIORITY_QUEUE_H */
