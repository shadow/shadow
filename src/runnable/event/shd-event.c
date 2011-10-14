/**
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

RunnableVTable event_vtable = {
	(RunnableRunFunc) shadowevent_run,
	(RunnableFreeFunc) shadowevent_free,
	MAGIC_VALUE
};

void shadowevent_init(Event* event, EventVTable* vtable) {
	g_assert(event && vtable);

	runnable_init(&(event->super), &event_vtable);

	MAGIC_INIT(event);
	MAGIC_INIT(vtable);

	event->vtable = vtable;
}

static void _event_updateDelay(Event* event) {
	vsocket_mgr_tp vs_mgr = event->node->vsocket_mgr;
	g_assert(vs_mgr);

	if(event->ownerID != event->node->id) {
		/* i didnt create the event. the delay attached is someone elses.
		 * this is the first i've seen of this event. take ownership and
		 * update the cpu delay to mine. */
		event->ownerID = event->node->id;
		event->cpuDelayPosition = vcpu_get_delay(vs_mgr->vcpu);
	}

	/* set our current position so any calls to read/write knows how
	 * much delay we've already absorbed.
	 */
	vcpu_set_absorbed(vs_mgr->vcpu, event->cpuDelayPosition);
}

static gboolean _event_reschedule(Event* event) {
	guint64 current_delay = vcpu_get_delay(event->node->vsocket_mgr->vcpu);

	if(event->cpuDelayPosition > current_delay) {
		/* impossible for our cpu to lose delay */
		error("vci_exec_event: delay on event (%lu) is greater than our CPU delay (%lu). Killing it. Things probably wont work right.\n", event->cpuDelayPosition, current_delay);
		/* free the event */
		return TRUE;
	}

	guint64 nanos_offset = current_delay - event->cpuDelayPosition;
	g_assert(nanos_offset > 0);

	event->cpuDelayPosition += nanos_offset;
	worker_scheduleEvent(event, nanos_offset, event->node->id);

	debug("vci_exec_event: event blocked on CPU, rescheduled for %lu nanoseconds from now\n", nanos_offset);

	/* dont free the event */
	return FALSE;
}

gboolean shadowevent_run(gpointer data) {
	Event* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	_event_updateDelay(event);

	/* check if we are allowed to execute or have to wait for cpu delays */
	if(vcpu_is_blocking(event->node->vsocket_mgr->vcpu)) {
		/* this event is delayed due to cpu, so reschedule it */
		return _event_reschedule(event);
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
