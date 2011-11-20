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

	/* (protocol,port)-to-transport bindings */
	GHashTable* boundTransports;

	/* NIC input queue */
	GQueue* inBuffer;
	gsize inBufferSize;
	gsize inBufferLength;

	/* Transports wanting to send data out */
	GQueue* sendableTransports;

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

	/* incoming packets get passed along to transports */
	interface->boundTransports = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, descriptor_unref);

	/* transports tell us when they want to start sending */
	interface->sendableTransports = g_queue_new();

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

	g_hash_table_destroy(interface->boundTransports);
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

	if(g_hash_table_lookup(interface->boundTransports, GINT_TO_POINTER(key))) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void networkinterface_associate(NetworkInterface* interface, Transport* transport) {
	MAGIC_ASSERT(interface);

	gint key = transport_getAssociationKey(transport);

	/* make sure there is no collision */
	g_assert(!networkinterface_isAssociated(interface, key));

	/* insert to our storage */
	g_hash_table_replace(interface->boundTransports, GINT_TO_POINTER(key), transport);
	descriptor_ref(transport);
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

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* a packet arrived. lets try to receive or buffer it */
	guint packetLength = packet_getPayloadLength(packet);
	if(packetLength <= (interface->inBufferSize - interface->inBufferLength)) {
		/* we have space to buffer it */
		packet_ref(packet);
		g_queue_push_tail(interface->inBuffer, packet);
		interface->inBufferLength += packetLength;

		/* we need a trigger if we are not currently receiving */
		if(!(interface->flags & NIF_RECEIVING)) {
			networkinterface_received(interface);
		}
	} else {
		/* buffers are full, drop packet */
		_networkinterface_dropInboundPacket(interface, packet);
	}
}

void networkinterface_received(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* try to receive the next packet if we have any */
	if(g_queue_get_length(interface->inBuffer) < 1) {
		/* nothing to receive right now.
		 * any new arrivals can now immediately trigger a receive event */
		interface->flags &= ~NIF_RECEIVING;
		return;
	}

	SimulationTime now = worker_getPrivate()->clock_now;
	SimulationTime absorbInterval = now - interface->lastTimeReceived;

	if(absorbInterval > 0) {
		/* decide how much delay we get to absorb based on the passed time */
		gdouble newConsumed = interface->receiveNanosecondsConsumed - absorbInterval;
		interface->receiveNanosecondsConsumed = MAX(0, newConsumed);
	}

	interface->lastTimeReceived = now;
	interface->flags |= NIF_RECEIVING;

	/* batch receive packets for processing */
	GQueue* packetBatch = g_queue_new();
	while(interface->receiveNanosecondsConsumed < CONFIG_RECEIVE_BATCH_TIME &&
			g_queue_get_length(interface->inBuffer) > 0) {
		/* get the next packet, dont unref because we'll be reffing it again */
		Packet* packet = g_queue_pop_head(interface->inBuffer);

		/* free up buffer space */
		guint length = packet_getPayloadLength(packet);
		interface->inBufferLength -= length;

		/* batch the packet for processing this round, still reffed from before */
		g_queue_push_tail(packetBatch, packet);

		/* consumed more bandwidth */
		length += packet_getHeaderSize(packet);
		interface->receiveNanosecondsConsumed += length * interface->timePerByteDown;
	}

	/* now process batch of packets */
	while(g_queue_get_length(packetBatch) > 0) {
		Packet* packet = g_queue_pop_head(packetBatch);

		/* hand it off to the correct transport layer */
		gint key = packet_getAssociationKey(packet);
		Transport* transport = g_hash_table_lookup(interface->boundTransports, GINT_TO_POINTER(key));
		gboolean accepted = transport_pushInPacket(transport, packet);
		if(!accepted) {
			/* transport can not handle it now, so drop it */
			_networkinterface_dropInboundPacket(interface, packet);
		}
	}

	/* batch queue better be empty now */
	if(g_queue_get_length(packetBatch) > 0) {
		critical("not all packets processed");
	}
	g_queue_free(packetBatch);

	/* update state */
	SimulationTime delay = (SimulationTime) floor(interface->receiveNanosecondsConsumed);
	if(delay >= SIMTIME_ONE_NANOSECOND) {
		/* call back when the packets are 'received' */
		InterfaceReceivedEvent* event = interfacereceived_new(interface);
		/* event destination is our node */
		worker_scheduleEvent((Event*)event, delay, 0);
	} else {
		/* not enough delays for our time granularity */
		interface->flags &= ~NIF_RECEIVING;
	}
}

void networkinterface_packetDropped(NetworkInterface* interface, Packet* packet) {
	MAGIC_ASSERT(interface);

	/* someone dropped a packet belonging to our interface */
	// TODO
}

void networkinterface_wantsSend(NetworkInterface* interface, Transport* transport) {
	MAGIC_ASSERT(interface);

	/* track the new transport for sending if not already tracking */
	if(!g_queue_find(interface->sendableTransports, transport)) {
		descriptor_ref(transport);
		g_queue_push_tail(interface->sendableTransports, transport);
	}

	/* trigger a send if we are currently idle */
	if(!(interface->flags & NIF_SENDING)) {
		networkinterface_sent(interface);
	}
}

void networkinterface_sent(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);

	/* we just finished sending a packet, now try to send the next one */
	if(g_queue_get_length(interface->sendableTransports) < 1) {
		/* nothing to send right now.
		 * any new arrivals can now immediately trigger a send event */
		interface->flags &= ~NIF_SENDING;
		return;
	}

	SimulationTime now = worker_getPrivate()->clock_now;
	SimulationTime absorbInterval = now - interface->lastTimeSent;

	if(absorbInterval > 0) {
		/* decide how much delay we get to absorb based on the passed time */
		gdouble newConsumed = interface->sendNanosecondsConsumed - absorbInterval;
		interface->receiveNanosecondsConsumed = MAX(0, newConsumed);
	}

	interface->lastTimeSent = now;
	interface->flags |= NIF_SENDING;

	/* batch outgoing packet transmissions */
	while(interface->sendNanosecondsConsumed < CONFIG_RECEIVE_BATCH_TIME &&
			g_queue_get_length(interface->sendableTransports) > 0) {
		/* do round robin on all ready transports.
		 * dont unref until we know we wont be returning it to the queue. */
		Transport* transport = g_queue_pop_head(interface->sendableTransports);

		/* check if it was closed in between sends */
		if(descriptor_getStatus((Descriptor*) transport) & DS_STALE) {
			gint* handleRef = descriptor_getHandleReference((Descriptor*) transport);
			debug("dereferencing stale descriptor %i", *handleRef);
			descriptor_unref(transport);
			continue;
		}

		Packet* packet = transport_pullOutPacket(transport);
		if(packet) {
			/* will send to network, consumed more bandwidth */
			guint length = packet_getPayloadLength(packet);
			length += packet_getHeaderSize(packet);
			interface->sendNanosecondsConsumed += length * interface->timePerByteUp;

			if(networkinterface_getIPAddress(interface) == packet_getDestinationIP(packet)) {
				/* packet will arrive on our own interface */
				PacketArrivedEvent* event = packetarrived_new(packet);
				/* event destination is our node */
				worker_scheduleEvent((Event*)event, 1, 0);
			} else {
				/* let the network schedule with appropriate delays */
				network_schedulePacket(interface->network, packet);
			}

			/* might have more packets, and is still reffed from before */
			g_queue_push_tail(interface->sendableTransports, transport);
		} else {
			/* transport has no more packets, unref it from round robin queue */
			descriptor_unref((Descriptor*) transport);
		}
	}

	/* update state */
	SimulationTime delay = (SimulationTime) floor(interface->sendNanosecondsConsumed);
	if(delay >= SIMTIME_ONE_NANOSECOND) {
		/* call back when the packets are 'sent' */
		InterfaceSentEvent* event = interfacesent_new(interface);
		/* event destination is our node */
		worker_scheduleEvent((Event*)event, delay, 0);
	} else {
		/* not enough delays for our time granularity */
		interface->flags &= ~NIF_SENDING;
	}
}
