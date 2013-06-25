/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-safe structure representing a data/network packet */

typedef struct _PacketLocalHeader PacketLocalHeader;
struct _PacketLocalHeader {
	enum ProtocolLocalFlags flags;
	gint sourceDescriptorHandle;
	gint destinationDescriptorHandle;
	in_port_t port;
};

typedef struct _PacketUDPHeader PacketUDPHeader;
struct _PacketUDPHeader {
	enum ProtocolUDPFlags flags;
	in_addr_t sourceIP;
	in_port_t sourcePort;
	in_addr_t destinationIP;
	in_port_t destinationPort;
};

struct _Packet {
	GMutex lock;
	guint referenceCount;

	enum ProtocolType protocol;
	gpointer header;
	gpointer payload;
	guint payloadLength;

	/* tracks application priority so we flush packets from the interface to
	 * the wire in the order intended by the application. this is used in
	 * the default FIFO network interface scheduling discipline.
	 * smaller values have greater priority.
	 */
	gdouble priority;

	MAGIC_DECLARE;
};

Packet* packet_new(gconstpointer payload, gsize payloadLength) {
	Packet* packet = g_new0(Packet, 1);
	MAGIC_INIT(packet);

	g_mutex_init(&(packet->lock));
	packet->referenceCount = 1;

	packet->payloadLength = payloadLength;
	if(payloadLength > 0) {
		/* if length is 0, this returns NULL */
		packet->payload = g_memdup(payload, payloadLength);

		/* application data needs a priority ordering for FIFO onto the wire */
		packet->priority = node_getNextPacketPriority(worker_getPrivate()->cached_node);
	}

	return packet;
}

static void _packet_free(Packet* packet) {
	MAGIC_ASSERT(packet);

	g_mutex_clear(&(packet->lock));
	if(packet->header) {
		g_free(packet->header);
	}
	if(packet->payload) {
		g_free(packet->payload);
	}

	MAGIC_CLEAR(packet);
	g_free(packet);
}

static void _packet_lock(Packet* packet) {
	MAGIC_ASSERT(packet);
	g_mutex_lock(&(packet->lock));
}

static void _packet_unlock(Packet* packet) {
	MAGIC_ASSERT(packet);
	g_mutex_unlock(&(packet->lock));
}

void packet_ref(Packet* packet) {
	_packet_lock(packet);
	(packet->referenceCount)++;
	_packet_unlock(packet);
}

void packet_unref(Packet* packet) {
	_packet_lock(packet);

	(packet->referenceCount)--;
	g_assert(packet->referenceCount >= 0);
	if(packet->referenceCount == 0) {
		_packet_unlock(packet);
		_packet_free(packet);
	} else {
		_packet_unlock(packet);
	}
}

gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data) {
	if(packet1 == packet2){
		MAGIC_ASSERT(packet1);
		MAGIC_ASSERT(packet2);
		return 0;
	}
	_packet_lock(packet1);
	_packet_lock(packet2);

	g_assert(packet1->protocol == PTCP && packet2->protocol == PTCP);
	gint result = ((PacketTCPHeader*)(packet1->header))->sequence < ((PacketTCPHeader*)(packet2->header))->sequence ? -1 : 1;

	_packet_unlock(packet2);
	_packet_unlock(packet1);
	return result;
}

void packet_setLocal(Packet* packet, enum ProtocolLocalFlags flags,
		gint sourceDescriptorHandle, gint destinationDescriptorHandle, in_port_t port) {
	_packet_lock(packet);
	g_assert(!(packet->header) && packet->protocol == PNONE);

	PacketLocalHeader* header = g_new0(PacketLocalHeader, 1);

	header->flags = flags;
	header->sourceDescriptorHandle = sourceDescriptorHandle;
	header->destinationDescriptorHandle = destinationDescriptorHandle;
	header->port = port;

	packet->header = header;
	packet->protocol = PLOCAL;
	_packet_unlock(packet);
}

void packet_setUDP(Packet* packet, enum ProtocolUDPFlags flags,
		in_addr_t sourceIP, in_port_t sourcePort,
		in_addr_t destinationIP, in_port_t destinationPort) {
	_packet_lock(packet);
	g_assert(!(packet->header) && packet->protocol == PNONE);

	PacketUDPHeader* header = g_new0(PacketUDPHeader, 1);

	header->flags = flags;
	header->sourceIP = sourceIP;
	header->sourcePort = sourcePort;
	header->destinationIP = destinationIP;
	header->destinationPort = destinationPort;

	packet->header = header;
	packet->protocol = PUDP;
	_packet_unlock(packet);
}

void packet_setTCP(Packet* packet, enum ProtocolTCPFlags flags,
		in_addr_t sourceIP, in_port_t sourcePort,
		in_addr_t destinationIP, in_port_t destinationPort,
		guint sequence, guint acknowledgement, guint window) {
	_packet_lock(packet);
	g_assert(!(packet->header) && packet->protocol == PNONE);

	PacketTCPHeader* header = g_new0(PacketTCPHeader, 1);

	header->flags = flags;
	header->sourceIP = sourceIP;
	header->sourcePort = sourcePort;
	header->destinationIP = destinationIP;
	header->destinationPort = destinationPort;
	header->sequence = sequence;
	header->acknowledgement = acknowledgement;
	header->window = window;

	packet->header = header;
	packet->protocol = PTCP;
	_packet_unlock(packet);
}

void packet_updateTCP(Packet* packet, guint acknowledgement, guint window) {
	_packet_lock(packet);
	g_assert(packet->header && (packet->protocol == PTCP));

	PacketTCPHeader* header = (PacketTCPHeader*) packet->header;

	header->acknowledgement = acknowledgement;
	header->window = window;

	_packet_unlock(packet);
}

guint packet_getPayloadLength(Packet* packet) {
	/* not locked, read only */
	return packet->payloadLength;
}

gdouble packet_getPriority(Packet* packet) {
	/* not locked, read only */
	return packet->priority;
}

guint packet_getHeaderSize(Packet* packet) {
	_packet_lock(packet);
	guint size = packet->protocol == PUDP ? CONFIG_HEADER_SIZE_UDPIPETH :
			packet->protocol == PTCP ? CONFIG_HEADER_SIZE_TCPIPETH : 0;
	_packet_unlock(packet);
	return size;
}

in_addr_t packet_getDestinationIP(Packet* packet) {
	_packet_lock(packet);

	in_addr_t ip = 0;

	switch (packet->protocol) {
		case PLOCAL: {
			ip = htonl(INADDR_LOOPBACK);
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			ip = header->destinationIP;
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			ip = header->destinationIP;
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	_packet_unlock(packet);
	return ip;
}

in_addr_t packet_getSourceIP(Packet* packet) {
	_packet_lock(packet);

	in_addr_t ip = 0;

	switch (packet->protocol) {
		case PLOCAL: {
			ip = htonl(INADDR_LOOPBACK);
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			ip = header->sourceIP;
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			ip = header->sourceIP;
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	_packet_unlock(packet);
	return ip;
}

in_port_t packet_getSourcePort(Packet* packet) {
	_packet_lock(packet);

	in_port_t port = 0;

	switch (packet->protocol) {
		case PLOCAL: {
			PacketLocalHeader* header = packet->header;
			port = header->port;
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			port = header->sourcePort;
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			port = header->sourcePort;
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	_packet_unlock(packet);
	return port;
}

guint packet_copyPayload(Packet* packet, gsize payloadOffset, gpointer buffer, gsize bufferLength) {
	_packet_lock(packet);

	g_assert(payloadOffset <= packet->payloadLength);

	guint targetLength = packet->payloadLength - ((guint)payloadOffset);
	guint copyLength = MIN(targetLength, bufferLength);

	if(copyLength > 0) {
		g_memmove(buffer, packet->payload + payloadOffset, copyLength);
	}

	_packet_unlock(packet);
	return copyLength;
}

gint packet_getDestinationAssociationKey(Packet* packet) {
	_packet_lock(packet);

	in_port_t port = 0;
	switch (packet->protocol) {
		case PLOCAL: {
			PacketLocalHeader* header = packet->header;
			port = header->port;
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			port = header->destinationPort;
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			port = header->destinationPort;
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	gint key = PROTOCOL_DEMUX_KEY(packet->protocol, port);

	_packet_unlock(packet);
	return key;
}

gint packet_getSourceAssociationKey(Packet* packet) {
	_packet_lock(packet);

	in_port_t port = 0;
	switch (packet->protocol) {
		case PLOCAL: {
			PacketLocalHeader* header = packet->header;
			port = header->port;
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			port = header->sourcePort;
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			port = header->sourcePort;
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	gint key = PROTOCOL_DEMUX_KEY(packet->protocol, port);

	_packet_unlock(packet);
	return key;
}

void packet_getTCPHeader(Packet* packet, PacketTCPHeader* header) {
	_packet_lock(packet);

	g_assert(packet->protocol == PTCP);
	*header = *((PacketTCPHeader*)packet->header);

	_packet_unlock(packet);
}

gchar* packet_getString(Packet* packet) {
	_packet_lock(packet);

	GString* packetBuffer = g_string_new("");

	switch (packet->protocol) {
		case PLOCAL: {
			PacketLocalHeader* header = packet->header;
			g_string_append_printf(packetBuffer, "%i -> %i bytes %u",
					header->sourceDescriptorHandle, header->destinationDescriptorHandle,
					packet->payloadLength);
			break;
		}

		case PUDP: {
			PacketUDPHeader* header = packet->header;
			gchar* sourceIPString = address_ipToNewString(header->sourceIP);
			gchar* destinationIPString = address_ipToNewString(header->destinationIP);

			g_string_append_printf(packetBuffer, "%s:%u -> ",
					sourceIPString, ntohs(header->sourcePort));
			g_string_append_printf(packetBuffer, "%s:%u bytes %u",
					destinationIPString, ntohs( header->destinationPort),
					packet->payloadLength);

			g_free(sourceIPString);
			g_free(destinationIPString);
			break;
		}

		case PTCP: {
			PacketTCPHeader* header = packet->header;
			gchar* sourceIPString = address_ipToNewString(header->sourceIP);
			gchar* destinationIPString = address_ipToNewString(header->destinationIP);

			g_string_append_printf(packetBuffer, "%s:%u -> ",
					sourceIPString, ntohs(header->sourcePort));
			g_string_append_printf(packetBuffer, "%s:%u packet# %u ack# %u window %u bytes %u",
					destinationIPString, ntohs(header->destinationPort),
					header->sequence, header->acknowledgement, header->window, packet->payloadLength);

			g_free(sourceIPString);
			g_free(destinationIPString);
			break;
		}

		default: {
			error("unrecognized protocol");
			break;
		}
	}

	_packet_unlock(packet);
	return g_string_free(packetBuffer, FALSE);
}
