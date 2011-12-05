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

struct _TransportFunctionTable {
	DescriptorFunc close;
	DescriptorFunc free;
	TransportSendFunc send;
	TransportReceiveFunc receive;
	MAGIC_DECLARE;
};

struct _Transport {
	Descriptor super;
	TransportFunctionTable* vtable;

	MAGIC_DECLARE;
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle);

gssize transport_sendUserData(Transport* transport, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_port_t port);
gssize transport_receiveUserData(Transport* transport, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port);

#endif /* SHD_TRANSPORT_H_ */
