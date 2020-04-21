/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>

#include "core/support/definitions.h"
#include "host/descriptor/descriptor.h"
#include "host/descriptor/transport.h"
#include "utility/utility.h"

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

void transport_init(Transport* transport, TransportFunctionTable* vtable, DescriptorType type, gint handle) {
    utility_assert(transport && vtable);

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
