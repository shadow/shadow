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
#include "main/host/syscall_types.h"
#include "main/utility/utility.h"

static Transport* _transport_fromLegacyFile(LegacyFile* descriptor) {
    utility_debugAssert(legacyfile_getType(descriptor) == DT_TCPSOCKET ||
                        legacyfile_getType(descriptor) == DT_UDPSOCKET);
    return (Transport*)descriptor;
}

static void _transport_cleanup(LegacyFile* descriptor) {
    Transport* transport = _transport_fromLegacyFile(descriptor);
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);

    if (transport->vtable->cleanup) {
        transport->vtable->cleanup(descriptor);
    }
}

static void _transport_free(LegacyFile* descriptor) {
    Transport* transport = _transport_fromLegacyFile(descriptor);
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);

    // TODO: assertion errors will occur if the subclass uses the transpor
    // during the free call. This could be fixed by making all descriptor types
    // a direct child of the descriptor class.
    MAGIC_CLEAR(transport);
    transport->vtable->free(descriptor);
}

static void _transport_close(LegacyFile* descriptor, const Host* host) {
    Transport* transport = _transport_fromLegacyFile(descriptor);
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);
    transport->vtable->close(descriptor, host);
}

LegacyFileFunctionTable transport_functions = {
    _transport_close, _transport_cleanup, _transport_free, MAGIC_VALUE};

void transport_init(Transport* transport, TransportFunctionTable* vtable, LegacyFileType type) {
    utility_debugAssert(transport && vtable);

    legacyfile_init(&(transport->super), type, &transport_functions);

    MAGIC_INIT(transport);
    MAGIC_INIT(vtable);

    transport->vtable = vtable;
}

gssize transport_sendUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                              gsize nBytes, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);
    return transport->vtable->send(transport, thread, buffer, nBytes, ip, port);
}

gssize transport_receiveUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                                 gsize nBytes, in_addr_t* ip, in_port_t* port) {
    MAGIC_ASSERT(transport);
    MAGIC_ASSERT(transport->vtable);
    return transport->vtable->receive(transport, thread, buffer, nBytes, ip, port);
}
