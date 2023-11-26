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

int compatsocket_peekNextPacketPriority(const CompatSocket* socket, uint64_t* priorityOut) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET: {
            Packet* packet = legacysocket_peekNextOutPacket(socket->object.as_legacy_socket);
            if (packet != NULL) {
                *priorityOut = packet_getPriority(packet);
                return 0;
            }
            return -1;
        }
        case CST_INET_SOCKET:
            return inetsocket_peekNextPacketPriority(socket->object.as_inet_socket, priorityOut);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

bool compatsocket_hasDataToSend(const CompatSocket* socket) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET:
            return legacysocket_peekNextOutPacket(socket->object.as_legacy_socket) != NULL;
        case CST_INET_SOCKET:
            return inetsocket_hasDataToSend(socket->object.as_inet_socket);
        case CST_NONE: utility_panic("Unexpected CompatSocket type");
    }

    utility_panic("Invalid CompatSocket type");
}

void compatsocket_pushInPacket(const CompatSocket* socket, const Host* host, Packet* packet,
                               CEmulatedTime recvTime) {
    switch (socket->type) {
        case CST_LEGACY_SOCKET:
            return legacysocket_pushInPacket(socket->object.as_legacy_socket, host, packet);
        case CST_INET_SOCKET:
            return inetsocket_pushInPacket(socket->object.as_inet_socket, packet, recvTime);
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
