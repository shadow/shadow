/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _InterfaceSentEvent {
    Event super;
    NetworkInterface* interface;
    MAGIC_DECLARE;
};

EventFunctionTable interfacesent_functions = {
    (EventRunFunc) interfacesent_run,
    (EventFreeFunc) interfacesent_free,
    MAGIC_VALUE
};

InterfaceSentEvent* interfacesent_new(NetworkInterface* interface) {
    InterfaceSentEvent* event = g_new0(InterfaceSentEvent, 1);
    MAGIC_INIT(event);

    shadowevent_init(&(event->super), &interfacesent_functions);

    event->interface = interface;

    return event;
}

void interfacesent_run(InterfaceSentEvent* event, Host* node) {
    MAGIC_ASSERT(event);

    debug("event started");

    networkinterface_sent(event->interface);

    debug("event finished");
}

void interfacesent_free(InterfaceSentEvent* event) {
    MAGIC_ASSERT(event);
    MAGIC_CLEAR(event);
    g_free(event);
}
