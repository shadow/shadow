/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_EVENT_H_
#define SHD_EVENT_H_

#include "shadow.h"

/* An event for a local virtual host, i.e.,
 * a host running on the same slave machine as the event initiator.
 * (These are packets sent between hosts on the same machine.) */
typedef struct _Event Event;

Event* event_new_(Task* task, SimulationTime time, gpointer srcHost, gpointer dstHost);
void event_ref(Event* event);
void event_unref(Event* event);

void event_execute(Event* event);
gint event_compare(const Event* a, const Event* b, gpointer userData);

gpointer event_getHost(Event* event);
SimulationTime event_getTime(Event* event);
void event_setTime(Event* event, SimulationTime time);
void event_setSequence(Event* event, guint64 sequence);

#endif /* SHD_EVENT_H_ */
