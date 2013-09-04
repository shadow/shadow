/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _StartApplicationEvent {
	Event super;
	Application* application;
	MAGIC_DECLARE;
};

EventFunctionTable startapplication_functions = {
	(EventRunFunc) startapplication_run,
	(EventFreeFunc) startapplication_free,
	MAGIC_VALUE
};

StartApplicationEvent* startapplication_new(Application* application) {
	StartApplicationEvent* event = g_new0(StartApplicationEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init((Event*) event, &startapplication_functions);

	event->application = application;

	return event;
}

void startapplication_run(StartApplicationEvent* event, Host* node) {
	MAGIC_ASSERT(event);

	host_startApplication(node, event->application);
}

void startapplication_free(StartApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
