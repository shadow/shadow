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

typedef void (*TransportSendFunc)(Transport* transport);
typedef void (*TransportFreeFunc)(Transport* transport);

struct _TransportFunctionTable {
	TransportSendFunc send;
	TransportFreeFunc free;
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

	MAGIC_DECLARE;
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle);
void transport_free(Transport* transport);

gboolean transport_isBound(Transport* transport);
void transport_setBinding(Transport* transport, in_addr_t boundAddress, in_port_t port);
gint transport_getAssociationKey(Transport* transport);

gboolean transport_pushInPacket(Transport* transport, Packet* packet);
Packet* transport_pullOutPacket(Transport* transport);

#endif /* SHD_TRANSPORT_H_ */
