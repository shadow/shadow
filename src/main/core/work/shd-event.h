/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_EVENT_H_
#define SHD_EVENT_H_

#include "shadow.h"

/* A basic event connected to a local virtual host. */
typedef struct _Event Event;

Event* event_new_(Task* task, SimulationTime time);
void event_ref(Event* event);
void event_unref(Event* event);

void event_execute(Event* event);
gint event_compare(const Event* a, const Event* b, gpointer userData);

SimulationTime event_getTime(Event* event);
void event_setTime(Event* event, SimulationTime time);
void event_setSequence(Event* event, guint64 sequence);

#endif /* SHD_EVENT_H_ */
