/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _StopApplicationEvent {
	Event super;
	Process* application;
	MAGIC_DECLARE;
};

EventFunctionTable stopapplication_functions = {
	(EventRunFunc) stopapplication_run,
	(EventFreeFunc) stopapplication_free,
	MAGIC_VALUE
};

StopApplicationEvent* stopapplication_new(Process* application) {
	StopApplicationEvent* event = g_new0(StopApplicationEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init((Event*) event, &stopapplication_functions);

	event->application = application;

	return event;
}

void stopapplication_run(StopApplicationEvent* event, Host* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	host_stopApplication(node, event->application);

	debug("event finished");
}

void stopapplication_free(StopApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
