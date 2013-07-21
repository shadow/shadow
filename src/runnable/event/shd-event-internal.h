/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_EVENT_INTERNAL_H_
#define SHD_EVENT_INTERNAL_H_

#include "shadow.h"

/*
 * A basic event connected to a specific node. This is meant
 * to be extended by most other events.
 */
struct _Event {
	Runnable super;
	EventFunctionTable* vtable;
	SimulationTime time;
	SimulationTime sequence;
	gpointer node; /* XXX: type is "Node*" */

	GQuark ownerID;
	MAGIC_DECLARE;
};

/*
 * Virtual function table for base event, storing pointers to required
 * callable functions.
 */
struct _EventFunctionTable {
	EventRunFunc run;
	EventFreeFunc free;
	MAGIC_DECLARE;
};

#endif /* SHD_EVENT_INTERNAL_H_ */
