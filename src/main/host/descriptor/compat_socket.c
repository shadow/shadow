/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/compat_socket.h"

#include <stdlib.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/routing/packet.h"

CompatSocket compatsocket_fromLegacySocket(LegacySocket* socket) {
    CompatSocket new_socket = {
        .type = CST_LEGACY_SOCKET,
        .object.as_legacy_socket = socket,
    };
    return new_socket;
}

CompatSocket compatsocket_fromInetSocket(const InetSocket* socket) {
    CompatSocket new_socket = {
        .type = CST_INET_SOCKET,
        .object.as_inet_socket = socket,
    };
    return new_socket;
}

uintptr_t compatsocket_getCanonicalHandle(const CompatSocket* socket) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET: return (uintptr_t)(void*)socket->object.as_legacy_socket;
        case CST_INET_SOCKET: return inetsocket_getCanonicalHandle(socket->object.as_inet_socket);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}
