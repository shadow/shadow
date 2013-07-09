/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
	/* events already scheduled get priority over new events */
	return (a->time > b->time) ? +1 : (a->time < b->time) ? -1 :
			(a->sequence > b->sequence) ? +1 : (a->sequence < b->sequence) ? -1 : 0;
}

void shadowevent_free(Event* event) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);

	MAGIC_CLEAR(event);
	event->vtable->free(event);
}
