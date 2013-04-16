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

#include "shadow.h"

void transport_free(Transport* transport) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);

	MAGIC_CLEAR(transport);
	transport->vtable->free((Descriptor*)transport);
}

void transport_close(Transport* transport) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	transport->vtable->close((Descriptor*)transport);
}

DescriptorFunctionTable transport_functions = {
	(DescriptorFunc) transport_close,
	(DescriptorFunc) transport_free,
	MAGIC_VALUE
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle) {
	g_assert(transport && vtable);

	descriptor_init(&(transport->super), type, &transport_functions, handle);

	MAGIC_INIT(transport);
	MAGIC_INIT(vtable);

	transport->vtable = vtable;

}

gssize transport_sendUserData(Transport* transport, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	return transport->vtable->send(transport, buffer, nBytes, ip, port);
}

gssize transport_receiveUserData(Transport* transport, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);
	return transport->vtable->receive(transport, buffer, nBytes, ip, port);
}
