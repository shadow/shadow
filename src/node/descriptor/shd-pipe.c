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

struct _Pipe {
	Transport super;

	MAGIC_DECLARE;
};

gboolean pipe_pushInPacket(Pipe* pipe, Packet* packet) {
	MAGIC_ASSERT(pipe);
	return FALSE;
}

Packet* pipe_pullOutPacket(Pipe* pipe) {
	MAGIC_ASSERT(pipe);
	return NULL;
}

gssize pipe_sendUserData(Pipe* pipe, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(pipe);

	return -1;
}

gssize pipe_receiveUserData(Pipe* pipe, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(pipe);
	return -1;
}

void pipe_free(Pipe* pipe) {
	MAGIC_ASSERT(pipe);

	MAGIC_CLEAR(pipe);
	g_free(pipe);
}

TransportFunctionTable pipe_functions = {
	(DescriptorFreeFunc) pipe_free,
	(TransportSendFunc) pipe_sendUserData,
	(TransportReceiveFunc) pipe_receiveUserData,
	(TransportPushFunc) pipe_pushInPacket,
	(TransportPullFunc) pipe_pullOutPacket,
	MAGIC_VALUE
};

Pipe* pipe_new(gint handle) {
	Pipe* pipe = g_new0(Pipe, 1);
	MAGIC_INIT(pipe);

	transport_init(&(pipe->super), &pipe_functions, DT_PIPE, handle);

	return pipe;
}

gint pipe_getHandles(Pipe* pipe, gint* handleX, gint* handleY) {
	MAGIC_ASSERT(pipe);

	return -1;
}
