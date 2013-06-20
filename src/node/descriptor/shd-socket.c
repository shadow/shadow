/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shadow.h"

void socket_free(gpointer data) {
	Socket* socket = data;
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);


	if(socket->peerString) {
		g_free(socket->peerString);
	}
	if(socket->boundString) {
		g_free(socket->boundString);
	}

	while(g_queue_get_length(socket->inputBuffer) > 0) {
		packet_unref(g_queue_pop_head(socket->inputBuffer));
	}
	g_queue_free(socket->inputBuffer);

	while(g_queue_get_length(socket->outputBuffer) > 0) {
		packet_unref(g_queue_pop_head(socket->outputBuffer));
	}
	g_queue_free(socket->outputBuffer);

	MAGIC_CLEAR(socket);
	socket->vtable->free((Descriptor*)socket);
}

void socket_close(Socket* socket) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	socket->vtable->close((Descriptor*)socket);

	Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
	Descriptor* descriptor = (Descriptor *)socket;
	tracker_removeSocket(tracker, descriptor->handle);
}

gssize socket_sendUserData(Socket* socket, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	return socket->vtable->send((Transport*)socket, buffer, nBytes, ip, port);
}

gssize socket_receiveUserData(Socket* socket, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	return socket->vtable->receive((Transport*)socket, buffer, nBytes, ip, port);
}

TransportFunctionTable socket_functions = {
	(DescriptorFunc) socket_close,
	(DescriptorFunc) socket_free,
	(TransportSendFunc) socket_sendUserData,
	(TransportReceiveFunc) socket_receiveUserData,
	MAGIC_VALUE
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle,
		guint receiveBufferSize, guint sendBufferSize) {
	g_assert(socket && vtable);

	transport_init(&(socket->super), &socket_functions, type, handle);

	MAGIC_INIT(socket);
	MAGIC_INIT(vtable);

	socket->vtable = vtable;

	socket->protocol = type == DT_TCPSOCKET ? PTCP : type == DT_UDPSOCKET ? PUDP : PLOCAL;
	socket->inputBuffer = g_queue_new();
	socket->inputBufferSize = receiveBufferSize;
	socket->outputBuffer = g_queue_new();
	socket->outputBufferSize = sendBufferSize;

	Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
	Descriptor* descriptor = (Descriptor *)socket;
	tracker_addSocket(tracker, descriptor->handle, socket->inputBufferSize, socket->outputBufferSize);
}

/* interface functions, implemented by subtypes */

gboolean socket_isFamilySupported(Socket* socket, sa_family_t family) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	return socket->vtable->isFamilySupported(socket, family);
}

gint socket_connectToPeer(Socket* socket, in_addr_t ip, in_port_t port, sa_family_t family) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);

	Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
	Descriptor* descriptor = (Descriptor *)socket;
	tracker_updateSocketPeer(tracker, descriptor->handle, ip, ntohs(port));

	return socket->vtable->connectToPeer(socket, ip, port, family);
}

void socket_droppedPacket(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	socket->vtable->dropped(socket, packet);
}

gboolean socket_pushInPacket(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	return socket->vtable->process(socket, packet);
}

/* functions implemented by socket */

Packet* socket_pullOutPacket(Socket* socket) {
	return socket_removeFromOutputBuffer(socket);
}

Packet* socket_peekNextPacket(const Socket* socket) {
	MAGIC_ASSERT(socket);
	return g_queue_peek_head(socket->outputBuffer);
}

gint socket_getPeerName(Socket* socket, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(socket);
	g_assert(ip && port);

	if(socket->peerIP == 0 || socket->peerPort == 0) {
		return ENOTCONN;
	}

	*ip = socket->peerIP;
	*port = socket->peerPort;

	return 0;
}

void socket_setPeerName(Socket* socket, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(socket);

	socket->peerIP = ip;
	socket->peerPort = port;

	/* store the new ascii name of this peer */
	if(socket->peerString) {
		g_free(socket->peerString);
	}
	gchar* ipString = address_ipToNewString(ip);
	GString* stringBuffer = g_string_new(ipString);
	g_free(ipString);
	g_string_append_printf(stringBuffer, ":%u", ntohs(port));
	socket->peerString = g_string_free(stringBuffer, FALSE);
}

gint socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(socket);
	g_assert(ip && port);

	/* boundAddress could be 0 (INADDR_NONE), so just check port */
	if(socket->boundPort == 0) {
		return ENOTCONN;
	}

	*ip = socket->boundAddress;
	*port = socket->boundPort;

	return 0;
}

void socket_setSocketName(Socket* socket, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(socket);
	socket_setBinding(socket, ip, port);

	/* children of server sockets must not have the same key as the parent
	 * otherwise when the child is closed, the parent's interface assoication
	 * will be removed.
	 *
	 * @todo: this should be handled more elegantly.
	 */
	socket->associationKey = 0;
}

in_addr_t socket_getBinding(Socket* socket) {
	MAGIC_ASSERT(socket);
	if(socket->flags & SF_BOUND) {
		return socket->boundAddress;
	} else {
		return 0;
	}
}

gboolean socket_isBound(Socket* socket) {
	MAGIC_ASSERT(socket);
	return (socket->flags & SF_BOUND) ? TRUE : FALSE;
}

void socket_setBinding(Socket* socket, in_addr_t boundAddress, in_port_t port) {
	MAGIC_ASSERT(socket);
	socket->boundAddress = boundAddress;
	socket->boundPort = port;

	/* store the new ascii name of this socket endpoint */
	if(socket->boundString) {
		g_free(socket->boundString);
	}

	gchar* ipString = address_ipToNewString(boundAddress);
	GString* stringBuffer = g_string_new(ipString);
	g_free(ipString);
	g_string_append_printf(stringBuffer, ":%u (descriptor %i)", ntohs(port), socket->super.super.handle);
	socket->boundString = g_string_free(stringBuffer, FALSE);

	socket->associationKey = PROTOCOL_DEMUX_KEY(socket->protocol, port);
	socket->flags |= SF_BOUND;
}

gint socket_getAssociationKey(Socket* socket) {
	MAGIC_ASSERT(socket);
	g_assert((socket->flags & SF_BOUND));
	return socket->associationKey;
}

gsize socket_getInputBufferSpace(Socket* socket) {
	MAGIC_ASSERT(socket);
	g_assert(socket->inputBufferSize >= socket->inputBufferLength);
	return (socket->inputBufferSize - socket->inputBufferLength);
}

gboolean socket_addToInputBuffer(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);

	/* check if the packet fits */
	guint length = packet_getPayloadLength(packet);
	if(length > socket_getInputBufferSpace(socket)) {
		return FALSE;
	}

	/* add to our queue */
	g_queue_push_tail(socket->inputBuffer, packet);
	socket->inputBufferLength += length;

	/* update the tracker input buffer stats */
	Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
	Descriptor* descriptor = (Descriptor *)socket;
	tracker_updateSocketInputBuffer(tracker, descriptor->handle, socket->inputBufferLength, socket->inputBufferSize);

	/* we just added a packet, so we are readable */
	if(socket->inputBufferLength > 0) {
		descriptor_adjustStatus((Descriptor*)socket, DS_READABLE, TRUE);
	}

	return TRUE;
}

Packet* socket_removeFromInputBuffer(Socket* socket) {
	MAGIC_ASSERT(socket);

	/* see if we have any packets */
	Packet* packet = g_queue_pop_head(socket->inputBuffer);
	if(packet) {
		/* just removed a packet */
		guint length = packet_getPayloadLength(packet);
		socket->inputBufferLength -= length;

		/* update the tracker input buffer stats */
		Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
		Descriptor* descriptor = (Descriptor *)socket;
		tracker_updateSocketInputBuffer(tracker, descriptor->handle, socket->inputBufferLength, socket->inputBufferSize);

		/* we are not readable if we are now empty */
		if(socket->inputBufferLength <= 0) {
			descriptor_adjustStatus((Descriptor*)socket, DS_READABLE, FALSE);
		}
	}

	return packet;
}

gsize socket_getOutputBufferSpace(Socket* socket) {
	MAGIC_ASSERT(socket);
	g_assert(socket->outputBufferSize >= socket->outputBufferLength);
	return (socket->outputBufferSize - socket->outputBufferLength);
}

gboolean socket_addToOutputBuffer(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);

	/* check if the packet fits */
	guint length = packet_getPayloadLength(packet);
	if(length > socket_getOutputBufferSpace(socket)) {
		return FALSE;
	}

	/* add to our queue */
	g_queue_push_tail(socket->outputBuffer, packet);
	socket->outputBufferLength += length;

	/* update the tracker input buffer stats */
	Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
	Descriptor* descriptor = (Descriptor *)socket;
	tracker_updateSocketOutputBuffer(tracker, descriptor->handle, socket->outputBufferLength, socket->outputBufferSize);

	/* we just added a packet, we are no longer writable if full */
	if(socket_getOutputBufferSpace(socket) <= 0) {
		descriptor_adjustStatus((Descriptor*)socket, DS_WRITABLE, FALSE);
	}

	/* tell the interface to include us when sending out to the network */
	in_addr_t ip = packet_getSourceIP(packet);
	NetworkInterface* interface = node_lookupInterface(worker_getPrivate()->cached_node, ip);
	networkinterface_wantsSend(interface, socket);

	return TRUE;
}

Packet* socket_removeFromOutputBuffer(Socket* socket) {
	MAGIC_ASSERT(socket);

	/* see if we have any packets */
	Packet* packet = g_queue_pop_head(socket->outputBuffer);
	if(packet) {
		/* just removed a packet */
		guint length = packet_getPayloadLength(packet);
		socket->outputBufferLength -= length;

		/* update the tracker input buffer stats */
		Tracker* tracker = node_getTracker(worker_getPrivate()->cached_node);
		Descriptor* descriptor = (Descriptor *)socket;
		tracker_updateSocketOutputBuffer(tracker, descriptor->handle, socket->outputBufferLength, socket->outputBufferSize);

		/* we are writable if we now have space */
		if(socket_getOutputBufferSpace(socket) > 0) {
			descriptor_adjustStatus((Descriptor*)socket, DS_WRITABLE, TRUE);
		}
	}

	return packet;
}
