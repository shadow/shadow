/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _StopApplicationEvent {
	Event super;
	Application* application;
	MAGIC_DECLARE;
};

EventFunctionTable stopapplication_functions = {
	(EventRunFunc) stopapplication_run,
	(EventFreeFunc) stopapplication_free,
	MAGIC_VALUE
};

StopApplicationEvent* stopapplication_new(Application* application) {
	StopApplicationEvent* event = g_new0(StopApplicationEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &stopapplication_functions);

	event->application = application;

	return event;
}

void stopapplication_run(StopApplicationEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	node_stopApplication(node, event->application);
}

void stopapplication_free(StopApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
