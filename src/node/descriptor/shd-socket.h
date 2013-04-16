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
gboolean socket_isBound(Socket* socket);
gint socket_getAssociationKey(Socket* socket);

gboolean socket_pushInPacket(Socket* socket, Packet* packet);
Packet* socket_pullOutPacket(Socket* socket);
Packet* socket_peekNextPacket(const Socket* socket);
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
