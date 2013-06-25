/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
