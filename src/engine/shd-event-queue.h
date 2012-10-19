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

#ifndef SHD_EVENT_QUEUE_H_
#define SHD_EVENT_QUEUE_H_


typedef struct _EventQueue EventQueue;

EventQueue* eventqueue_new();
void eventqueue_free(EventQueue* eventq);
void eventqueue_push(EventQueue* eventq, Event* event);
Event* eventqueue_peek(EventQueue* eventq);
Event* eventqueue_pop(EventQueue* eventq);


#endif /* SHD_EVENT_QUEUE_H_ */
