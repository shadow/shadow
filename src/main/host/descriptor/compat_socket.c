/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/compat_socket.h"

#include <stdlib.h>

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/descriptor/socket.h"
#include "main/utility/tagged_ptr.h"
#include "support/logger/logger.h"

CompatSocket compatsocket_fromLegacySocket(Socket* socket) {
    CompatSocket new_socket = {
        .type = CST_LEGACY_SOCKET,
        .object.as_legacy_socket = socket,
    };
    return new_socket;
}

CompatSocket compatsocket_cloneRef(const CompatSocket* socket) {
    CompatSocket new_socket = {
        .type = socket->type,
        .object = socket->object,
    };

    if (new_socket.type == CST_LEGACY_SOCKET) {
        descriptor_ref(new_socket.object.as_legacy_socket);
    } else {
        error("Unexpected CompatSocket type");
    }

    return new_socket;
}

void compatsocket_drop(const CompatSocket* socket) {
    if (socket->type == CST_LEGACY_SOCKET) {
        descriptor_unref(socket->object.as_legacy_socket);
    } else {
        error("Unexpected CompatSocket type");
    }
}

uintptr_t compatsocket_toTagged(const CompatSocket* socket) {
    CompatSocketTypes type = socket->type;
    CompatSocketObject object = socket->object;

    const void* object_ptr;

    if (socket->type == CST_LEGACY_SOCKET) {
        object_ptr = object.as_legacy_socket;
    } else {
        error("Unexpected CompatSocket type");
    }

    return tagPtr(object_ptr, type);
}

CompatSocket compatsocket_fromTagged(uintptr_t ptr) {
    CompatSocketTypes type;
    CompatSocketObject object;

    uintptr_t tag = 0;
    void* object_ptr = untagPtr(ptr, &tag);

    if (tag == CST_LEGACY_SOCKET) {
        object.as_legacy_socket = object_ptr;
    } else {
        error("Unexpected socket pointer tag");
        abort();
    }

    type = tag;

    CompatSocket socket = {
        .type = type,
        .object = object,
    };

    return socket;
}

ProtocolType compatsocket_getProtocol(const CompatSocket* socket) {
    if (socket->type == CST_LEGACY_SOCKET) {
        return socket_getProtocol(socket->object.as_legacy_socket);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}

bool compatsocket_getPeerName(const CompatSocket* socket, in_addr_t* ip, in_port_t* port) {
    if (socket->type == CST_LEGACY_SOCKET) {
        return socket_getPeerName(socket->object.as_legacy_socket, ip, port);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}

bool compatsocket_getSocketName(const CompatSocket* socket, in_addr_t* ip, in_port_t* port) {
    if (socket->type == CST_LEGACY_SOCKET) {
        return socket_getSocketName(socket->object.as_legacy_socket, ip, port);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}

const Packet* compatsocket_peekNextOutPacket(const CompatSocket* socket) {
    if (socket->type == CST_LEGACY_SOCKET) {
        return socket_peekNextOutPacket(socket->object.as_legacy_socket);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}

void compatsocket_pushInPacket(const CompatSocket* socket, Packet* packet) {
    if (socket->type == CST_LEGACY_SOCKET) {
        socket_pushInPacket(socket->object.as_legacy_socket, packet);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}

Packet* compatsocket_pullOutPacket(const CompatSocket* socket) {
    if (socket->type == CST_LEGACY_SOCKET) {
        return socket_pullOutPacket(socket->object.as_legacy_socket);
    } else {
        error("Unexpected CompatSocket type");
        abort();
    }
}
