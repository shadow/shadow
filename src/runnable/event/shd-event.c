/*
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

	Worker* worker = worker_getPrivate();
	Node* node = event->node;

	if(engine_getConfig(worker->cached_engine)->cpuThreshold >= 0) {
		/* check if we are allowed to execute or have to wait for cpu delays */
		CPU* cpu = node_getCPU(node);
		cpu_updateTime(cpu, event->time);

		if(cpu_isBlocked(cpu)) {
			SimulationTime cpuDelay = cpu_getDelay(cpu);
			debug("event blocked on CPU, rescheduled for %lu nanoseconds from now", cpuDelay);
			/* this event is delayed due to cpu, so reschedule it to ourselves */
			worker_scheduleEvent(event, cpuDelay, 0);
			/* dont free it, it needs to run again */
			return FALSE;
		}
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
