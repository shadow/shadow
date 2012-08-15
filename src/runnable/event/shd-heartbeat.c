/**
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

EventFunctionTable heartbeat_functions = {
	(EventRunFunc) heartbeat_run,
	(EventFreeFunc) heartbeat_free,
	MAGIC_VALUE
};

struct _HeartbeatEvent {
	Event super;
	Tracker* tracker;
	MAGIC_DECLARE;
};

HeartbeatEvent* heartbeat_new(Tracker* tracker) {
	HeartbeatEvent* event = g_new0(HeartbeatEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &heartbeat_functions);
	event->tracker = tracker;

	return event;
}

void heartbeat_run(HeartbeatEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	tracker_heartbeat(event->tracker);
}

void heartbeat_free(HeartbeatEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
