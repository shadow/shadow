/*
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
