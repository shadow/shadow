/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _PacketDroppedEvent {
    Event super;
    Packet* packet;
    MAGIC_DECLARE;
};

EventFunctionTable packetdropped_functions = {
    (EventRunFunc) packetdropped_run,
    (EventFreeFunc) packetdropped_free,
    MAGIC_VALUE
};

PacketDroppedEvent* packetdropped_new(Packet* packet) {
    PacketDroppedEvent* event = g_new0(PacketDroppedEvent, 1);
    MAGIC_INIT(event);

    shadowevent_init(&(event->super), &packetdropped_functions);

    packet_ref(packet);
    event->packet = packet;

    return event;
}

void packetdropped_run(PacketDroppedEvent* event, Host* node) {
    MAGIC_ASSERT(event);

    debug("event started");

    in_addr_t ip = packet_getSourceIP(event->packet);
    NetworkInterface* interface = host_lookupInterface(node, ip);
    networkinterface_packetDropped(interface, event->packet);

    debug("event finished");
}

void packetdropped_free(PacketDroppedEvent* event) {
    MAGIC_ASSERT(event);

    packet_unref(event->packet);

    MAGIC_CLEAR(event);
    g_free(event);
}
