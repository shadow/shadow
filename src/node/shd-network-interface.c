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

#include "shadow.h"

enum NetworkInterfaceFlags {
	NIF_NONE = 0,
	NIF_SENDING = 1 << 0,
	NIF_RECEIVING = 1 << 1,
};

struct _NetworkInterface {
	enum NetworkInterfaceFlags flags;

	Network* network;
	Address* address;

	guint32 bwDownKiBps;
	gdouble timePerByteDown;
	guint32 bwUpKiBps;
	gdouble timePerByteUp;

	/* (protocol,port)-to-socket bindings */
	GHashTable* boundSockets;

	/* NIC input queue */
	GQueue* inBuffer;
	gsize inBufferSize;
	gsize inBufferLength;

	/* Transports wanting to send data out */
	GQueue* sendableSockets;

	/* bandwidth accounting */
	SimulationTime lastTimeReceived;
	SimulationTime lastTimeSent;
	gdouble sendNanosecondsConsumed;
	gdouble receiveNanosecondsConsumed;
	MAGIC_DECLARE;
};

NetworkInterface* networkinterface_new(Network* network, GQuark address, gchar* name,
		guint32 bwDownKiBps, guint32 bwUpKiBps) {
	NetworkInterface* interface = g_new0(NetworkInterface, 1);
	MAGIC_INIT(interface);

	interface->network = network;
	interface->address = address_new(address, (const gchar*) name);

	/* interface speeds */
	interface->bwUpKiBps = bwUpKiBps;
	gdouble bytesPerSecond = (gdouble)(bwUpKiBps * 1024);
	interface->timePerByteUp = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);
	interface->bwDownKiBps = bwDownKiBps;
	bytesPerSecond = (gdouble)(bwDownKiBps * 1024);
	interface->timePerByteDown = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);

	/* incoming packet buffer */
	interface->inBuffer = g_queue_new();
	/* @todo: set as configuration option, test effect of changing sizes */
	interface->inBufferSize = bytesPerSecond;

	/* incoming packets get passed along to sockets */
	interface->boundSockets = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, descriptor_unref);

	/* sockets tell us when they want to start sending */
	interface->sendableSockets = g_queue_new();

	/* log status */
	char buffer[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &address, buffer, INET_ADDRSTRLEN);

	debug("bringing up network interface '%s' at '%s', %u KiBps up and %u KiBps down",
			name, buffer, bwUpKiBps, bwDownKiBps);

	return interface;
}

void networkinterface_free(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* unref all packets sitting in our input buffer */
	while(interface->inBuffer && g_queue_get_length(interface->inBuffer)) {
		Packet* packet = g_queue_pop_head(interface->inBuffer);
		packet_unref(packet);
	}
	g_queue_free(interface->inBuffer);

	/* unref all sockets wanting to send */
	while(interface->sendableSockets && g_queue_get_length(interface->sendableSockets)) {
		Socket* socket = g_queue_pop_head(interface->sendableSockets);
		descriptor_unref(socket);
	}
	g_queue_free(interface->sendableSockets);

	g_hash_table_destroy(interface->boundSockets);
	address_free(interface->address);

	MAGIC_CLEAR(interface);
	g_free(interface);
}

in_addr_t networkinterface_getIPAddress(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return address_toNetworkIP(interface->address);
}

gchar* networkinterface_getIPName(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return address_toHostIPString(interface->address);
}

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return interface->bwUpKiBps;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return interface->bwDownKiBps;
}

gboolean networkinterface_isAssociated(NetworkInterface* interface, gint key) {
	MAGIC_ASSERT(interface);

	if(g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void networkinterface_associate(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	gint key = socket_getAssociationKey(socket);

	/* make sure there is no collision */
	g_assert(!networkinterface_isAssociated(interface, key));

	/* insert to our storage */
	g_hash_table_replace(interface->boundSockets, GINT_TO_POINTER(key), socket);
	descriptor_ref(socket);
}

void networkinterface_disassociate(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	gint key = socket_getAssociationKey(socket);

	/* we will no longer receive packets for this port, this unrefs descriptor */
	g_hash_table_remove(interface->boundSockets, GINT_TO_POINTER(key));
}

static void _networkinterface_dropInboundPacket(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* drop incoming packet that traversed the network link from source */

	if(networkinterface_getIPAddress(interface) == packet_getSourceIP(packet)) {
		/* packet is on our own interface, so event destination is our node */
		PacketDroppedEvent* event = packetdropped_new(packet);
		worker_scheduleEvent((Event*)event, 1, 0);
	} else {
		/* let the network schedule the event with appropriate delays */
		network_scheduleRetransmit(interface->network, packet);
	}
}

static void _networkinterface_scheduleNextReceive(NetworkInterface* interface) {
	/* the next packet needs to be received according to bandwidth limitations.
	 * we need to spend time receiving it before processing it. */
	Packet* packet = g_queue_peek_head(interface->inBuffer);
	if(!packet) {
		return;
	}

	/* calculate how long it will take to 'receive' this packet */
	guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
	SimulationTime delay = (SimulationTime) floor(length * interface->timePerByteDown);

	/* call back when the packet is 'received' */
	InterfaceReceivedEvent* event = interfacereceived_new(interface);

	/* event destination is our node */
	worker_scheduleEvent((Event*)event, delay, 0);

	/* we are now in the process of receiving */
	interface->flags |= NIF_RECEIVING;
}

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* a packet arrived. lets try to receive or buffer it */
//	if(interface->inBufferLength < CONFIG_INTERFACE_BUFFER_LENGTH) {
	if(g_queue_get_length(interface->inBuffer) < 10) {
		/* we have space to buffer it */
		g_queue_push_tail(interface->inBuffer, packet);
		interface->inBufferLength += packet_getPayloadLength(packet) + packet_getHeaderSize(packet);

		/* we need a trigger if we are not currently receiving */
		if(!(interface->flags & NIF_RECEIVING)) {
			_networkinterface_scheduleNextReceive(interface);
		}
	} else {
		/* buffers are full, drop packet */
		_networkinterface_dropInboundPacket(interface, packet);
	}
}

void networkinterface_received(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* we just finished receiving a packet and can now process it */
	interface->flags &= ~NIF_RECEIVING;

	/* get the next packet, dont unref because we'll be reffing it again */
	Packet* packet = g_queue_pop_head(interface->inBuffer);
	g_assert(packet);
	interface->inBufferLength -= packet_getPayloadLength(packet) + packet_getHeaderSize(packet);

	/* hand it off to the correct socket layer */
	gint key = packet_getDestinationAssociationKey(packet);
	Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

	/* if the socket closed, just drop the packet */
	if(socket) {
		gboolean needsRetransmiit = socket_pushInPacket(socket, packet);
		if(needsRetransmiit) {
			/* socket can not handle it now, so drop it */
			_networkinterface_dropInboundPacket(interface, packet);
		}
	}

	/* check if we have another to receive */
	_networkinterface_scheduleNextReceive(interface);
}

void networkinterface_packetDropped(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/*
	 * someone dropped a packet belonging to our interface
	 * hand it off to the correct socket layer
	 */
	gint key = packet_getSourceAssociationKey(packet);
	Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

	/* just ignore if the socket closed in the meantime */
	if(socket) {
		socket_droppedPacket(socket, packet);
	}
}

static void _networkinterface_scheduleNextSend(NetworkInterface* interface) {
	/* the next packet needs to be sent according to bandwidth limitations.
	 * we need to spend time sending it before sending the next it.
	 * loop until we find a socket that has something to send */
	while(g_queue_get_length(interface->sendableSockets) > 0) {
		/* do round robin on all ready sockets */
		Socket* socket = g_queue_pop_head(interface->sendableSockets);

		Packet* packet = socket_pullOutPacket(socket);
		if(!packet) {
			/* socket has no more packets, unref it from round robin queue */
			descriptor_unref((Descriptor*) socket);
			continue;
		}

		/* calculate how long it will take to 'send' this packet */
		guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
		SimulationTime delay = (SimulationTime) floor(length * interface->timePerByteUp);

		/* call back when the packet is 'sent' */
		InterfaceSentEvent* event = interfacesent_new(interface);
		/* event destination is our node */
		worker_scheduleEvent((Event*)event, delay, 0);

		/* socket might have more packets, and is still reffed from before */
		g_queue_push_tail(interface->sendableSockets, socket);

		/* now actually send the packet somewhere */
		interface->flags |= NIF_SENDING;

		if(networkinterface_getIPAddress(interface) == packet_getDestinationIP(packet)) {
			/* packet will arrive on our own interface */
			PacketArrivedEvent* event = packetarrived_new(packet);
			/* event destination is our node */
			worker_scheduleEvent((Event*)event, 1, 0);
		} else {
			/* let the network schedule with appropriate delays */
			network_schedulePacket(interface->network, packet);
		}

		/* successfully sent */
		break;
	}
}

void networkinterface_wantsSend(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	/* track the new socket for sending if not already tracking */
	if(!g_queue_find(interface->sendableSockets, socket)) {
		descriptor_ref(socket);
		g_queue_push_tail(interface->sendableSockets, socket);
	}

	/* trigger a send if we are currently idle */
	if(!(interface->flags & NIF_SENDING)) {
		_networkinterface_scheduleNextSend(interface);
	}
}

void networkinterface_sent(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* we just finished sending a packet */
	interface->flags &= ~NIF_SENDING;

	/* now try to send the next one */
	_networkinterface_scheduleNextSend(interface);
}
