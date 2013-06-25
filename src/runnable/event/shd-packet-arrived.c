/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _PacketArrivedEvent {
	Event super;
	Packet* packet;
	MAGIC_DECLARE;
};

EventFunctionTable packetarrived_functions = {
	(EventRunFunc) packetarrived_run,
	(EventFreeFunc) packetarrived_free,
	MAGIC_VALUE
};

PacketArrivedEvent* packetarrived_new(Packet* packet) {
	PacketArrivedEvent* event = g_new0(PacketArrivedEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &packetarrived_functions);

	packet_ref(packet);
	event->packet = packet;

	return event;
}

void packetarrived_run(PacketArrivedEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	in_addr_t ip = packet_getDestinationIP(event->packet);
	NetworkInterface* interface = node_lookupInterface(node, ip);
	networkinterface_packetArrived(interface, event->packet);

	debug("event finished");
}

void packetarrived_free(PacketArrivedEvent* event) {
	MAGIC_ASSERT(event);

	packet_unref(event->packet);

	MAGIC_CLEAR(event);
	g_free(event);
}
