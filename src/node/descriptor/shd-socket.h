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

#ifndef SHD_SOCKET_H_
#define SHD_SOCKET_H_

typedef struct _Socket Socket;
typedef struct _SocketFunctionTable SocketFunctionTable;

typedef void (*SocketSendFunc)(Socket* transport);
typedef void (*SocketFreeFunc)(Socket* transport);

struct _SocketFunctionTable {
	SocketSendFunc send;
	SocketFreeFunc free;
	MAGIC_DECLARE;
};

enum SocketFlags {
	SF_NONE = 0,
	SF_BOUND = 1 << 0,
};

struct _Socket {
	Transport super;
	SocketFunctionTable* vtable;

	enum SocketFlags flags;
	in_addr_t boundInterfaceIP;
	in_port_t boundPort;
	MAGIC_DECLARE;
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle);
void socket_free(gpointer data);

void socket_send(gpointer data);

gboolean socket_isBound(Socket* socket);
void socket_bindToInterface(Socket* socket, in_addr_t interfaceIP, in_port_t port);

#endif /* SHD_SOCKET_H_ */
