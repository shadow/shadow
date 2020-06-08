/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>

#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/transport.h"
#include "main/utility/utility.h"

void transport_free(Transport* transport) {
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);

    MAGIC_CLEAR(transport);
    transport->vtable->free((Descriptor*)transport);
}

gboolean transport_close(Transport* transport) {
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);
    return transport->vtable->close((Descriptor*)transport);
}

DescriptorFunctionTable transport_functions = {
    (DescriptorCloseFunc) transport_close,
    (DescriptorFreeFunc) transport_free,
    MAGIC_VALUE
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, DescriptorType type) {
    utility_assert(transport && vtable);

    descriptor_init(&(transport->super), type, &transport_functions);

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
