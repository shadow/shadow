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
