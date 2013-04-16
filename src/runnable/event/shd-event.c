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

#include "shadow.h"

RunnableFunctionTable event_functions = {
	(RunnableRunFunc) shadowevent_run,
	(RunnableFreeFunc) shadowevent_free,
	MAGIC_VALUE
};

void shadowevent_init(Event* event, EventFunctionTable* vtable) {
	g_assert(event && vtable);

	runnable_init(&(event->super), &event_functions);

	MAGIC_INIT(event);
	MAGIC_INIT(vtable);

	event->vtable = vtable;
}

gboolean shadowevent_run(Event* event) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);

	Node* node = event->node;

	/* check if we are allowed to execute or have to wait for cpu delays */
	CPU* cpu = node_getCPU(node);
	cpu_updateTime(cpu, event->time);

	if(cpu_isBlocked(cpu)) {
		SimulationTime cpuDelay = cpu_getDelay(cpu);
		debug("event blocked on CPU, rescheduled for %lu nanoseconds from now", cpuDelay);

		/* track the event delay time */
		tracker_addVirtualProcessingDelay(node_getTracker(node), cpuDelay);

		/* this event is delayed due to cpu, so reschedule it to ourselves */
		worker_scheduleEvent(event, cpuDelay, 0);

		/* dont free it, it needs to run again */
		return FALSE;
	}

	/* if we get here, its ok to execute the event */
	event->vtable->run(event, node);
	/* we've actually executed it, so its ok to free it */
	return TRUE;
}

gint shadowevent_compare(const Event* a, const Event* b, gpointer user_data) {
	MAGIC_ASSERT(a);
	MAGIC_ASSERT(b);
	/*
	 * @todo should events already scheduled get priority over new events?
	 */
	return a->time > b->time ? +1 : a->time == b->time ? 0 : -1;
}

void shadowevent_free(Event* event) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);

	MAGIC_CLEAR(event);
	event->vtable->free(event);
}
