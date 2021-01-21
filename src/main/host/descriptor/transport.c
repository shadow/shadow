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

static Transport* _transport_fromLegacyDescriptor(LegacyDescriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_TCPSOCKET ||
                   descriptor_getType(descriptor) == DT_UDPSOCKET ||
                   descriptor_getType(descriptor) == DT_UNIXSOCKET ||
                   descriptor_getType(descriptor) == DT_PIPE);
    return (Transport*)descriptor;
}

static void _transport_free(LegacyDescriptor* descriptor) {
    Transport* transport = _transport_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);

    // TODO: assertion errors will occur if the subclass uses the transpor
    // during the free call. This could be fixed by making all descriptor types
    // a direct child of the descriptor class.
    MAGIC_CLEAR(transport);
    transport->vtable->free(descriptor);
}

static gboolean _transport_close(LegacyDescriptor* descriptor) {
    Transport* transport = _transport_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);
    return transport->vtable->close(descriptor);
}

DescriptorFunctionTable transport_functions = {
    _transport_close, _transport_free, MAGIC_VALUE};

void transport_init(Transport* transport, TransportFunctionTable* vtable,
                    LegacyDescriptorType type) {
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
