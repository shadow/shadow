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

#ifndef SHD_TRANSPORT_H_
#define SHD_TRANSPORT_H_

#include "shadow.h"

typedef struct _Transport Transport;
typedef struct _TransportFunctionTable TransportFunctionTable;

typedef gssize (*TransportSendFunc)(Transport* transport, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port);
typedef gssize (*TransportReceiveFunc)(Transport* transport, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port);
typedef gboolean (*TransportProcessFunc)(Transport* transport, Packet* packet);

struct _TransportFunctionTable {
	DescriptorFreeFunc free;
	TransportSendFunc send;
	TransportReceiveFunc receive;
	TransportProcessFunc process;
	MAGIC_DECLARE;
};

enum TransportFlags {
	TF_NONE = 0,
	TF_BOUND = 1 << 0,
};

struct _Transport {
	Descriptor super;
	TransportFunctionTable* vtable;

	enum ProtocolType protocol;
	enum TransportFlags flags;
	in_addr_t boundAddress;
	in_port_t boundPort;
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

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle);

gboolean transport_isBound(Transport* transport);
void transport_setBinding(Transport* transport, in_addr_t boundAddress, in_port_t port);
gint transport_getAssociationKey(Transport* transport);

gboolean transport_pushInPacket(Transport* transport, Packet* packet);
Packet* transport_pullOutPacket(Transport* transport);
gssize transport_sendUserData(Transport* transport, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_port_t port);
gssize transport_receiveUserData(Transport* transport, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port);

gboolean transport_addToInputBuffer(Transport* transport, Packet* packet);
Packet* transport_removeFromInputBuffer(Transport* transport);
gboolean transport_addToOutputBuffer(Transport* transport, Packet* packet);
Packet* transport_removeFromOutputBuffer(Transport* transport);

#endif /* SHD_TRANSPORT_H_ */
