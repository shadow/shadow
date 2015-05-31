/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_EVENT_H_
#define SHD_EVENT_H_

#include "shadow.h"

typedef struct _Event Event;
typedef struct _EventFunctionTable EventFunctionTable;

/* required functions */
typedef void (*EventRunFunc)(Event* event, gpointer node); /* XXX: type is "Node*" */
typedef void (*EventFreeFunc)(Event* event);

void shadowevent_init(Event* event, EventFunctionTable* vtable);
gboolean shadowevent_run(Event* event);
void shadowevent_setSequence(Event* event, SimulationTime sequence);
SimulationTime shadowevent_getTime(Event* event);
void shadowevent_setTime(Event* event, SimulationTime time);
gpointer shadowevent_getNode(Event* event); /* XXX: return type is "Node*" */
void shadowevent_setNode(Event* event, gpointer node); /* XXX: type is "Node*" */
gint shadowevent_compare(const Event* a, const Event* b, gpointer user_data);
void shadowevent_free(Event* event);

#endif /* SHD_EVENT_H_ */
