/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
