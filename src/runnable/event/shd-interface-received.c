/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _InterfaceReceivedEvent {
	Event super;
	NetworkInterface* interface;
	MAGIC_DECLARE;
};

EventFunctionTable interfacereceived_functions = {
	(EventRunFunc) interfacereceived_run,
	(EventFreeFunc) interfacereceived_free,
	MAGIC_VALUE
};

InterfaceReceivedEvent* interfacereceived_new(NetworkInterface* interface) {
	InterfaceReceivedEvent* event = g_new0(InterfaceReceivedEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &interfacereceived_functions);

	event->interface = interface;

	return event;
}

void interfacereceived_run(InterfaceReceivedEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	networkinterface_received(event->interface);

	debug("event finished");
}

void interfacereceived_free(InterfaceReceivedEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
