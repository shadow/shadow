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

void socket_free(gpointer data) {
	Socket* socket = data;
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);

	if(socket->peerString) {
		g_free(socket->peerString);
	}

	MAGIC_CLEAR(socket);
	socket->vtable->free((Descriptor*)socket);
}

void socket_close(Socket* socket) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	socket->vtable->close((Descriptor*)socket);
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

gboolean socket_processPacket(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	return socket->vtable->process((Transport*)socket, packet);
}

void socket_droppedPacket(Socket* socket, Packet* packet) {
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);
	socket->vtable->dropped((Transport*)socket, packet);
}

TransportFunctionTable socket_functions = {
	(DescriptorFunc) socket_close,
	(DescriptorFunc) socket_free,
	(TransportSendFunc) socket_sendUserData,
	(TransportReceiveFunc) socket_receiveUserData,
	(TransportProcessFunc) socket_processPacket,
	(TransportDroppedPacketFunc) socket_droppedPacket,
	MAGIC_VALUE
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle) {
	g_assert(socket && vtable);

	transport_init(&(socket->super), &socket_functions, type, handle);

	MAGIC_INIT(socket);
	MAGIC_INIT(vtable);

	socket->vtable = vtable;
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
	return socket->vtable->connectToPeer(socket, ip, port, family);
}

/* functions implemented by socket */

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
	GString* stringBuffer = g_string_new(NTOA(ip));
	g_string_append_printf(stringBuffer, ":%u", ntohs(port));
	socket->peerString = g_string_free(stringBuffer, FALSE);
}

gint socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(socket);
	g_assert(ip && port);

	if(socket->super.boundAddress == 0 || socket->super.boundPort == 0) {
		return ENOTCONN;
	}

	*ip = socket->super.boundAddress;
	*port = socket->super.boundPort;

	return 0;
}

void socket_setSocketName(Socket* socket, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(socket);
	transport_setBinding(&(socket->super), ip, port);
}
