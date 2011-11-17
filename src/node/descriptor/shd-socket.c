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

void socket_send(gpointer data) {

}

void socket_free(gpointer data) {
	Socket* socket = data;
	MAGIC_ASSERT(socket);
	MAGIC_ASSERT(socket->vtable);

	MAGIC_CLEAR(socket);
	socket->vtable->free(socket);
}
