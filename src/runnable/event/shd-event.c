/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

gboolean shadowevent_run(gpointer data) {
	Event* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	SimulationTime cpuDelay = vcpu_adjustDelay(event->node->vsocket_mgr->vcpu, event->time);

	/* check if we are allowed to execute or have to wait for cpu delays */
	if(cpuDelay > 0) {
		debug("event blocked on CPU, rescheduled for %lu nanoseconds from now", cpuDelay);
		/* this event is delayed due to cpu, so reschedule it */
		worker_scheduleEvent(event, cpuDelay, event->node->id);
		/* dont free it, it needs to run again */
		return FALSE;
	} else {
		/* ok to execute the event */
		event->vtable->run(event, event->node);
		/* we've actually executed it, so its ok to free it */
		return TRUE;
	}
}

gint shadowevent_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Event* ea = a;
	const Event* eb = b;
	MAGIC_ASSERT(ea);
	MAGIC_ASSERT(eb);
	return ea->time > eb->time ? +1 : ea->time == eb->time ? 0 : -1;
}

void shadowevent_free(gpointer data) {
	Event* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	MAGIC_CLEAR(event);
	event->vtable->free(event);
}
