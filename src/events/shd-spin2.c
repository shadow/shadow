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

#include <glib.h>
#include "shadow.h"

NodeEventVTable spin2_vtable = {
	(NodeEventExecuteFunc)spin2_execute,
	(NodeEventFreeFunc)spin2_free,
	MAGIC_VALUE
};

Spin2Event* spin2_new(guint seconds) {
	Spin2Event* event = g_new(Spin2Event, 1);
	MAGIC_INIT(event);

	nodeevent_init(&(event->super), &spin2_vtable);
	event->spin_seconds = seconds;

	return event;
}

void spin2_free(Spin2Event* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}

void spin2_execute(Spin2Event* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	debug("executing spin event for %u seconds", event->spin_seconds);

	guint64 i = 1000000 * event->spin_seconds;
	while(i--) {
		continue;
	}

	Spin2Event* se = spin2_new(event->spin_seconds);
	SimulationTime t = 1;
	worker_scheduleNodeEvent((NodeEvent*)se, t, node->node_id);

	// FIXME: the following breaks
//	worker_scheduleNodeEvent((NodeEvent*)se, t, ((node->node_id++) % 100));
}
