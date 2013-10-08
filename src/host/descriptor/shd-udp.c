/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _UDP {
	Socket super;

	MAGIC_DECLARE;
};

gboolean udp_isFamilySupported(UDP* udp, sa_family_t family) {
	MAGIC_ASSERT(udp);
	return (family == AF_INET || family == AF_UNSPEC) ? TRUE : FALSE;
}

gint udp_connectToPeer(UDP* udp, in_addr_t ip, in_port_t port, sa_family_t family) {
	MAGIC_ASSERT(udp);


	/* ip/port specifies the default destination for packets */
	if(family == AF_UNSPEC) {
		/* dissolve our existing defaults */
		socket_setPeerName(&(udp->super), 0, 0);
	} else {
		/* set new defaults */
		socket_setPeerName(&(udp->super), ip, port);
	}

	return 0;
}

/* return TRUE if the packet should be retransmitted */
gboolean udp_processPacket(UDP* udp, Packet* packet) {
	MAGIC_ASSERT(udp);

	/* UDP packet contains data for user and can be buffered immediately */
	if(packet_getPayloadLength(packet) > 0) {
		return socket_addToInputBuffer((Socket*)udp, packet);
	}
	return FALSE;
}

void udp_droppedPacket(UDP* udp, Packet* packet) {
	MAGIC_ASSERT(udp);
	/* udp doesnt care about reliability */
}

/*
 * this function builds a UDP packet and sends to the virtual node given by the
 * ip and port parameters. this function assumes that the socket is already
 * bound to a local port, no matter if that happened explicitly or implicitly.
 */
gssize udp_sendUserData(UDP* udp, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(udp);

	gsize space = socket_getOutputBufferSpace(&(udp->super));
	if(space < nBytes) {
		/* not enough space to buffer the data */
		return -1;
	}

	/* break data into segments and send each in a packet */
	gsize maxPacketLength = CONFIG_DATAGRAM_MAX_SIZE;
	gsize remaining = nBytes;
	gsize offset = 0;

	/* create as many packets as needed */
	while(remaining > 0) {
		gsize copyLength = MIN(maxPacketLength, remaining);

		/* use default destination if none was specified */
		in_addr_t destinationIP = (ip != 0) ? ip : udp->super.peerIP;
		in_port_t destinationPort = (port != 0) ? port : udp->super.peerPort;

		/* create the UDP packet */
		Packet* packet = packet_new(buffer + offset, copyLength);
		packet_setUDP(packet, PUDP_NONE, socket_getBinding(&(udp->super)),
				udp->super.boundPort, destinationIP, destinationPort);

		/* buffer it in the transport layer, to be sent out when possible */
		gboolean success = socket_addToOutputBuffer((Socket*) udp, packet);

		/* counter maintenance */
		if(success) {
			remaining -= copyLength;
			offset += copyLength;
		} else {
			warning("unable to send UDP packet");
			break;
		}
	}

	/* update the tracker output buffer stats */
	Tracker* tracker = host_getTracker(worker_getCurrentHost());
	Socket* socket = (Socket* )udp;
	Descriptor* descriptor = (Descriptor *)socket;
	gsize outLength = socket_getOutputBufferLength(socket);
	gsize outSize = socket_getOutputBufferSize(socket);
	tracker_updateSocketOutputBuffer(tracker, descriptor->handle, outLength, outSize);

	debug("buffered %"G_GSIZE_FORMAT" outbound UDP bytes from user", offset);

	return (gssize) offset;
}

gssize udp_receiveUserData(UDP* udp, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(udp);

	Packet* packet = socket_removeFromInputBuffer((Socket*)udp);
	if(!packet) {
		return -1;
	}

	/* copy lesser of requested and available amount to application buffer */
	guint packetLength = packet_getPayloadLength(packet);
	gsize copyLength = MIN(nBytes, packetLength);
	guint bytesCopied = packet_copyPayload(packet, 0, buffer, copyLength);

	utility_assert(bytesCopied == copyLength);

	/* fill in address info */
	if(ip) {
		*ip = packet_getSourceIP(packet);
	}
	if(port) {
		*port = packet_getSourcePort(packet);
	}

	/* destroy packet, throwing away any bytes not claimed by the app */
	packet_unref(packet);

	/* update the tracker output buffer stats */
	Tracker* tracker = host_getTracker(worker_getCurrentHost());
	Socket* socket = (Socket* )udp;
	Descriptor* descriptor = (Descriptor *)socket;
	gsize outLength = socket_getOutputBufferLength(socket);
	gsize outSize = socket_getOutputBufferSize(socket);
	tracker_updateSocketOutputBuffer(tracker, descriptor->handle, outLength, outSize);

	debug("user read %u inbound UDP bytes", bytesCopied);

	return (gssize)bytesCopied;
}

void udp_free(UDP* udp) {
	MAGIC_ASSERT(udp);

	MAGIC_CLEAR(udp);
	g_free(udp);
}

void udp_close(UDP* udp) {
	MAGIC_ASSERT(udp);
	host_closeDescriptor(worker_getCurrentHost(), udp->super.super.super.handle);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable udp_functions = {
	(DescriptorFunc) udp_close,
	(DescriptorFunc) udp_free,
	(TransportSendFunc) udp_sendUserData,
	(TransportReceiveFunc) udp_receiveUserData,
	(SocketProcessFunc) udp_processPacket,
	(SocketDroppedPacketFunc) udp_droppedPacket,
	(SocketIsFamilySupportedFunc) udp_isFamilySupported,
	(SocketConnectToPeerFunc) udp_connectToPeer,
	MAGIC_VALUE
};

UDP* udp_new(gint handle, guint receiveBufferSize, guint sendBufferSize) {
	UDP* udp = g_new0(UDP, 1);
	MAGIC_INIT(udp);

	socket_init(&(udp->super), &udp_functions, DT_UDPSOCKET, handle, receiveBufferSize, sendBufferSize);

	/* we are immediately active because UDP doesnt wait for accept or connect */
	descriptor_adjustStatus((Descriptor*) udp, DS_ACTIVE|DS_WRITABLE, TRUE);

	return udp;
}
