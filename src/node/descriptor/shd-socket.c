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

TransportFunctionTable socket_functions = {
	(TransportSendFunc) socket_send,
	(TransportFreeFunc) socket_free,
	MAGIC_VALUE
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle) {
	g_assert(socket && vtable);

	transport_init(&(socket->super), &socket_functions, type, handle);

	MAGIC_INIT(socket);
	MAGIC_INIT(vtable);

	socket->vtable = vtable;
}

void socket_free(gpointer data) {
	Socket* socket = data;
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);

	MAGIC_CLEAR(socket);
	socket->vtable->free(socket);
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

void socket_send(Socket* socket) {

}

/* functions implemented by socket */

gboolean socket_isBound(Socket* socket) {
	MAGIC_ASSERT(socket);
	return (socket->flags & SF_BOUND) ? TRUE : FALSE;
}

void socket_bindToInterface(Socket* socket, in_addr_t interfaceIP, in_port_t port) {
	MAGIC_ASSERT(socket);
	g_assert(!(socket->flags & SF_BOUND));
	socket->boundInterfaceIP = interfaceIP;
	socket->boundPort = port;
	socket->flags |= SF_BOUND;
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

gint socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(socket);
	g_assert(ip && port);

	if(socket->boundInterfaceIP == 0 || socket->boundPort == 0) {
		return ENOTCONN;
	}

	*ip = socket->boundInterfaceIP;
	*port = socket->boundPort;

	return 0;
}
