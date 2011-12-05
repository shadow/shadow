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
typedef gboolean (*SocketProcessFunc)(Socket* socket, Packet* packet);
typedef void (*SocketDroppedPacketFunc)(Socket* socket, Packet* packet);

struct _SocketFunctionTable {
	DescriptorFunc close;
	DescriptorFunc free;
	TransportSendFunc send;
	TransportReceiveFunc receive;
	SocketProcessFunc process;
	SocketDroppedPacketFunc dropped;
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
	enum ProtocolType protocol;

	in_addr_t peerIP;
	in_addr_t peerPort;
	gchar* peerString;

	in_addr_t boundAddress;
	in_port_t boundPort;
	gchar* boundString;

	gint associationKey;

	/* buffering packets readable by user */
	GQueue* inputBuffer;
	gsize inputBufferSize;
	gsize inputBufferLength;

	/* buffering packets ready to send */
	GQueue* outputBuffer;
	gsize outputBufferSize;
	gsize outputBufferLength;

	MAGIC_DECLARE;
};

void socket_init(Socket* socket, SocketFunctionTable* vtable, enum DescriptorType type, gint handle);

in_addr_t socket_getBinding(Socket* socket);
void socket_setBinding(Socket* socket, in_addr_t boundAddress, in_port_t port);
gint socket_getAssociationKey(Socket* socket);

gboolean socket_pushInPacket(Socket* socket, Packet* packet);
Packet* socket_pullOutPacket(Socket* socket);
void socket_droppedPacket(Socket* socket, Packet* packet);

gsize socket_getInputBufferSpace(Socket* socket);
gboolean socket_addToInputBuffer(Socket* socket, Packet* packet);
Packet* socket_removeFromInputBuffer(Socket* socket);

gsize socket_getOutputBufferSpace(Socket* socket);
gboolean socket_addToOutputBuffer(Socket* socket, Packet* packet);
Packet* socket_removeFromOutputBuffer(Socket* socket);

gint socket_getPeerName(Socket* socket, in_addr_t* ip, in_port_t* port);
void socket_setPeerName(Socket* socket, in_addr_t ip, in_port_t port);
gint socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port);
void socket_setSocketName(Socket* socket, in_addr_t ip, in_port_t port);

gboolean socket_isFamilySupported(Socket* socket, sa_family_t family);
gint socket_connectToPeer(Socket* socket, in_addr_t ip, in_port_t port, sa_family_t family);

#endif /* SHD_SOCKET_H_ */
