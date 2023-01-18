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
#include "main/utility/tagged_ptr.h"

static void compatsockettypes_assertValid(CompatSocketTypes type) {
    switch (type) {
        case CST_LEGACY_SOCKET:
        case CST_INET_SOCKET:
        case CST_NONE: return;
    }
    utility_panic("Invalid CompatSocket type");
}

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

CompatSocket compatsocket_refAs(const CompatSocket* socket) {
    CompatSocket new_socket = {
        .type = socket->type,
        .object = socket->object,
    };

    switch (new_socket.type) {
        case CST_LEGACY_SOCKET: legacyfile_ref(new_socket.object.as_legacy_socket); break;
        case CST_INET_SOCKET:
            new_socket.object.as_inet_socket = inetsocket_cloneRef(new_socket.object.as_inet_socket);
            break;
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    compatsockettypes_assertValid(new_socket.type);

    return new_socket;
}

void compatsocket_unref(const CompatSocket* socket) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET: legacyfile_unref(socket->object.as_legacy_socket); break;
        case CST_INET_SOCKET: inetsocket_drop(socket->object.as_inet_socket); break;
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    compatsockettypes_assertValid(socket->type);
}

uintptr_t compatsocket_getCanonicalHandle(const CompatSocket* socket) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET: return (uintptr_t)(void*)socket->object.as_legacy_socket;
        case CST_INET_SOCKET: return inetsocket_getCanonicalHandle(socket->object.as_inet_socket);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

uintptr_t compatsocket_toTagged(const CompatSocket* socket) {
    CompatSocketTypes type = socket->type;
    CompatSocketObject object = socket->object;

    const void* object_ptr;

    switch (socket->type) {
        case CST_LEGACY_SOCKET: object_ptr = object.as_legacy_socket; break;
        case CST_INET_SOCKET: object_ptr = object.as_inet_socket; break;
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    compatsockettypes_assertValid(socket->type);

    return tagPtr(object_ptr, type);
}

CompatSocket compatsocket_fromTagged(uintptr_t ptr) {
    CompatSocketTypes type;
    CompatSocketObject object;

    uintptr_t tag = 0;
    void* object_ptr = untagPtr(ptr, &tag);

    switch (tag) {
        case CST_LEGACY_SOCKET: object.as_legacy_socket = object_ptr; break;
        case CST_INET_SOCKET: object.as_inet_socket = object_ptr; break;
        case CST_NONE: utility_panic("Unexpected socket pointer tag");
    }

    compatsockettypes_assertValid(tag);

    type = tag;

    CompatSocket socket = {
        .type = type,
        .object = object,
    };

    return socket;
}

const Packet* compatsocket_peekNextOutPacket(const CompatSocket* socket) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET:
            return legacysocket_peekNextOutPacket(socket->object.as_legacy_socket);
        case CST_INET_SOCKET:
            return inetsocket_peekNextOutPacket(socket->object.as_inet_socket);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

void compatsocket_pushInPacket(const CompatSocket* socket, const Host* host, Packet* packet) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET:
            return legacysocket_pushInPacket(socket->object.as_legacy_socket, host, packet);
        case CST_INET_SOCKET:
            return inetsocket_pushInPacket(socket->object.as_inet_socket, packet);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

Packet* compatsocket_pullOutPacket(const CompatSocket* socket, const Host* host) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET:
            return legacysocket_pullOutPacket(socket->object.as_legacy_socket, host);
        case CST_INET_SOCKET:
            return inetsocket_pullOutPacket(socket->object.as_inet_socket);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

void compatsocket_updatePacketHeader(const CompatSocket* socket, const Host* host, Packet* packet) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET: {
            LegacySocket* legacySocket = socket->object.as_legacy_socket;

            if (legacysocket_getProtocol(legacySocket) == PTCP) {
                TCP* tcp = (TCP*)legacySocket;
                tcp_networkInterfaceIsAboutToSendPacket(tcp, host, packet);
            }

            return;
        }
        case CST_INET_SOCKET:
            return inetsocket_updatePacketHeader(socket->object.as_inet_socket, packet);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}
