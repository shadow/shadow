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

#ifndef SHD_EVENT_H_
#define SHD_EVENT_H_

#include "shadow.h"

typedef struct _Event Event;
typedef struct _EventFunctionTable EventFunctionTable;

/* required functions */
typedef void (*EventRunFunc)(Event* event, gpointer node); /* XXX: type is "Node*" */
typedef void (*EventFreeFunc)(Event* event);

/*
 * Virtual function table for base event, storing pointers to required
 * callable functions.
 */
struct _EventFunctionTable {
	EventRunFunc run;
	EventFreeFunc free;
	MAGIC_DECLARE;
};

/*
 * A basic event connected to a specific node. This is meant
 * to be extended by most other events.
 */
struct _Event {
	Runnable super;
	EventFunctionTable* vtable;
	SimulationTime time;
	gpointer node; /* XXX: type is "Node*" */

	GQuark ownerID;
	MAGIC_DECLARE;
};

void shadowevent_init(Event* event, EventFunctionTable* vtable);
gboolean shadowevent_run(Event* event);
gint shadowevent_compare(const Event* a, const Event* b, gpointer user_data);
void shadowevent_free(Event* event);

#endif /* SHD_EVENT_H_ */
