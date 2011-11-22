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

typedef gboolean (*SocketIsFamilySupportedFunc)(Socket* socket, sa_family_t family);
typedef gint (*SocketConnectToPeerFunc)(Socket* socket, in_addr_t ip, in_port_t port, sa_family_t family);

struct _SocketFunctionTable {
	DescriptorFreeFunc free;
	TransportSendFunc send;
	TransportReceiveFunc receive;
	TransportProcessFunc process;
	SocketIsFamilySupportedFunc isFamilySupported;
	SocketConnectToPeerFunc connectToPeer;
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
	in_addr_t peerIP;
	in_addr_t peerPort;
	MAGIC_DECLARE;
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle);

gint socket_getPeerName(Socket* socket, in_addr_t* ip, in_port_t* port);
gint socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port);

gboolean socket_isFamilySupported(Socket* socket, sa_family_t family);
gint socket_connectToPeer(Socket* socket, in_addr_t ip, in_port_t port, sa_family_t family);

#endif /* SHD_SOCKET_H_ */
