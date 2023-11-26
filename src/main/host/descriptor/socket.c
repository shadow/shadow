/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <sys/un.h>

#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

static LegacySocket* _legacysocket_fromLegacyFile(LegacyFile* descriptor) {
    utility_debugAssert(legacyfile_getType(descriptor) == DT_TCPSOCKET);
    return (LegacySocket*)descriptor;
}

static void _legacysocket_cleanup(LegacyFile* descriptor) {
    LegacySocket* socket = _legacysocket_fromLegacyFile(descriptor);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);

    if (socket->vtable->cleanup) {
        socket->vtable->cleanup(descriptor);
    }
}

static void _legacysocket_free(LegacyFile* descriptor) {
    LegacySocket* socket = _legacysocket_fromLegacyFile(descriptor);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);


    if(socket->peerString) {
        g_free(socket->peerString);
    }
    if(socket->boundString) {
        g_free(socket->boundString);
    }
    if(socket->unixPath) {
        g_free(socket->unixPath);
    }

    while(g_queue_get_length(socket->inputBuffer) > 0) {
        packet_unref(g_queue_pop_head(socket->inputBuffer));
    }
    g_queue_free(socket->inputBuffer);

    while(g_queue_get_length(socket->outputBuffer) > 0) {
        packet_unref(g_queue_pop_head(socket->outputBuffer));
    }
    g_queue_free(socket->outputBuffer);

    while(g_queue_get_length(socket->outputControlBuffer) > 0) {
        packet_unref(g_queue_pop_head(socket->outputControlBuffer));
    }
    g_queue_free(socket->outputControlBuffer);

    // TODO: assertion errors will occur if the subclass uses the socket
    // during the free call. This could be fixed by making all descriptor types
    // a direct child of the descriptor class.
    MAGIC_CLEAR(socket);
    socket->vtable->free((LegacyFile*)socket);
}

static void _legacysocket_close(LegacyFile* descriptor, const Host* host) {
    LegacySocket* socket = _legacysocket_fromLegacyFile(descriptor);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);

    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_removeSocket(tracker, &compatSocket);
    }

    socket->vtable->close((LegacyFile*)socket, host);
}

gssize legacysocket_sendUserData(LegacySocket* socket, const Thread* thread,
                                 UntypedForeignPtr buffer, gsize nBytes, in_addr_t ip,
                                 in_port_t port) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->send(socket, thread, buffer, nBytes, ip, port);
}

gssize legacysocket_receiveUserData(LegacySocket* socket, const Thread* thread,
                                    UntypedForeignPtr buffer, gsize nBytes, in_addr_t* ip,
                                    in_port_t* port) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->receive(socket, thread, buffer, nBytes, ip, port);
}

LegacyFileFunctionTable socket_functions = {
    _legacysocket_close, _legacysocket_cleanup, _legacysocket_free, MAGIC_VALUE};

void legacysocket_init(LegacySocket* socket, const Host* host, SocketFunctionTable* vtable,
                       LegacyFileType type, guint receiveBufferSize, guint sendBufferSize) {
    utility_debugAssert(socket && vtable);

    legacyfile_init(&(socket->super), type, &socket_functions);

    MAGIC_INIT(socket);
    MAGIC_INIT(vtable);

    socket->vtable = vtable;

    utility_debugAssert(type == DT_TCPSOCKET);
    socket->protocol = type == PTCP;
    socket->inputBuffer = g_queue_new();
    socket->inputBufferSize = receiveBufferSize;
    socket->outputBuffer = g_queue_new();
    socket->outputControlBuffer = g_queue_new();
    socket->outputBufferSize = sendBufferSize;

    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_addSocket(tracker, &compatSocket, socket->protocol, socket->inputBufferSize,
                          socket->outputBufferSize);
    }
}

ProtocolType legacysocket_getProtocol(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->protocol;
}

/* interface functions, implemented by subtypes */

gboolean legacysocket_isFamilySupported(LegacySocket* socket, sa_family_t family) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->isFamilySupported(socket, family);
}

gint legacysocket_connectToPeer(LegacySocket* socket, const Host* host, in_addr_t ip,
                                in_port_t port, sa_family_t family) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);

    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_updateSocketPeer(tracker, &compatSocket, ip, ntohs(port));
    }

    return socket->vtable->connectToPeer(socket, host, ip, port, family);
}

void legacysocket_pushInPacket(LegacySocket* socket, const Host* host, Packet* packet) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_PROCESSED);
    socket->vtable->process(socket, host, packet);
}

void legacysocket_dropPacket(LegacySocket* socket, const Host* host, Packet* packet) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    socket->vtable->dropPacket(socket, host, packet);
}

/* functions implemented by socket */

Packet* legacysocket_pullOutPacket(LegacySocket* socket, const Host* host) {
    return legacysocket_removeFromOutputBuffer(socket, host);
}

Packet* legacysocket_peekNextOutPacket(const LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    if(!g_queue_is_empty(socket->outputControlBuffer)) {
        return g_queue_peek_head(socket->outputControlBuffer);
    } else {
        return g_queue_peek_head(socket->outputBuffer);
    }
}

Packet* legacysocket_peekNextInPacket(const LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return g_queue_peek_head(socket->inputBuffer);
}

gboolean legacysocket_getPeerName(LegacySocket* socket, in_addr_t* ip, in_port_t* port) {
    MAGIC_ASSERT(socket);

    if(socket->peerIP == 0 || socket->peerPort == 0) {
        return FALSE;
    }

    if(ip) {
        *ip = socket->peerIP;
    }
    if(port) {
        *port = socket->peerPort;
    }

    return TRUE;
}

void legacysocket_setPeerName(LegacySocket* socket, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(socket);

    socket->peerIP = ip;
    socket->peerPort = port;

    /* store the new ascii name of this peer */
    if(socket->peerString) {
        g_free(socket->peerString);
    }
    gchar* ipString = address_ipToNewString(ip);
    GString* stringBuffer = g_string_new(ipString);
    g_free(ipString);
    g_string_append_printf(stringBuffer, ":%u", ntohs(port));
    socket->peerString = g_string_free(stringBuffer, FALSE);
}

gboolean legacysocket_getSocketName(LegacySocket* socket, in_addr_t* ip, in_port_t* port) {
    MAGIC_ASSERT(socket);

    /* boundAddress could be 0 (INADDR_NONE), so just check port */
    if(!legacysocket_isBound(socket)) {
        return FALSE;
    }

    if(ip) {
        if(socket->boundAddress == htonl(INADDR_ANY) &&
                socket->peerIP && socket->peerIP == htonl(INADDR_LOOPBACK)) {
            *ip = htonl(INADDR_LOOPBACK);
        } else {
            *ip = socket->boundAddress;
        }
    }
    if(port) {
        *port = socket->boundPort;
    }

    return TRUE;
}

void legacysocket_setSocketName(LegacySocket* socket, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(socket);

    socket->boundAddress = ip;
    socket->boundPort = port;

    /* store the new ascii name of this socket endpoint */
    if(socket->boundString) {
        g_free(socket->boundString);
    }

    gchar* ipString = address_ipToNewString(ip);
    GString* stringBuffer = g_string_new(ipString);
    g_free(ipString);
    g_string_append_printf(stringBuffer, ":%u (descriptor %p)", ntohs(port), &socket->super);
    socket->boundString = g_string_free(stringBuffer, FALSE);

    /* the socket is now bound */
    socket->flags |= SF_BOUND;
}

gboolean legacysocket_isBound(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return (socket->flags & SF_BOUND) ? TRUE : FALSE;
}

gsize legacysocket_getInputBufferSpace(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    utility_debugAssert(socket->inputBufferSize >= socket->inputBufferLength);
    gsize bufferSize = legacysocket_getInputBufferSize(socket);
    if(bufferSize < socket->inputBufferLength) {
        return 0;
    } else {
        return bufferSize - socket->inputBufferLength;
    }
}

gsize legacysocket_getOutputBufferSpace(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    utility_debugAssert(socket->outputBufferSize >= socket->outputBufferLength);
    gsize bufferSize = legacysocket_getOutputBufferSize(socket);
    if(bufferSize < socket->outputBufferLength) {
        return 0;
    } else {
        return bufferSize - socket->outputBufferLength;
    }
}

gsize legacysocket_getInputBufferLength(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->inputBufferLength;
}

gsize legacysocket_getOutputBufferLength(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->outputBufferLength;
}

gsize legacysocket_getInputBufferSize(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->inputBufferSizePending > 0 ? socket->inputBufferSizePending : socket->inputBufferSize;
}

gsize legacysocket_getOutputBufferSize(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->outputBufferSizePending > 0 ? socket->outputBufferSizePending : socket->outputBufferSize;
}

void legacysocket_setInputBufferSize(LegacySocket* socket, gsize newSize) {
    MAGIC_ASSERT(socket);
    if(newSize >= socket->inputBufferLength) {
        socket->inputBufferSize = newSize;
        socket->inputBufferSizePending = 0;
    } else {
        /* ensure positive size, reduce size as buffer drains */
        socket->inputBufferSize = socket->inputBufferLength;
        socket->inputBufferSizePending = newSize;
    }
}

void legacysocket_setOutputBufferSize(LegacySocket* socket, gsize newSize) {
    MAGIC_ASSERT(socket);
    if(newSize >= socket->outputBufferLength) {
        socket->outputBufferSize = newSize;
        socket->outputBufferSizePending = 0;
    } else {
        /* ensure positive size, reduce size as buffer drains */
        socket->outputBufferSize = socket->outputBufferLength;
        socket->outputBufferSizePending = newSize;
    }
}

gboolean legacysocket_addToInputBuffer(LegacySocket* socket, const Host* host, Packet* packet) {
    MAGIC_ASSERT(socket);

    /* check if the packet fits */
    gsize length = packet_getPayloadSize(packet);
    if(length > legacysocket_getInputBufferSpace(socket)) {
        return FALSE;
    }

    /* add to our queue */
    g_queue_push_tail(socket->inputBuffer, packet);
    packet_ref(packet);
    socket->inputBufferLength += length;
    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_BUFFERED);

    /* update the tracker input buffer stats */
    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_updateSocketInputBuffer(
            tracker, &compatSocket, socket->inputBufferLength, socket->inputBufferSize);
    }

    /* we just added a packet, so we are readable */
    if(socket->inputBufferLength > 0) {
        legacyfile_adjustStatus((LegacyFile*)socket, STATUS_FILE_READABLE, TRUE);
    }

    return TRUE;
}

Packet* legacysocket_removeFromInputBuffer(LegacySocket* socket, const Host* host) {
    MAGIC_ASSERT(socket);

    /* see if we have any packets */
    Packet* packet = g_queue_pop_head(socket->inputBuffer);
    if(packet) {
        /* just removed a packet */
        gsize length = packet_getPayloadSize(packet);
        socket->inputBufferLength -= length;

        /* check if we need to reduce the buffer size */
        if(socket->inputBufferSizePending > 0) {
            legacysocket_setInputBufferSize(socket, socket->inputBufferSizePending);
        }

        /* update the tracker input buffer stats */
        Tracker* tracker = host_getTracker(host);
        if (tracker != NULL) {
            CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
            tracker_updateSocketInputBuffer(
                tracker, &compatSocket, socket->inputBufferLength, socket->inputBufferSize);
        }

        /* we are not readable if we are now empty */
        if(socket->inputBufferLength <= 0) {
            legacyfile_adjustStatus((LegacyFile*)socket, STATUS_FILE_READABLE, FALSE);
        }
    }

    return packet;
}

gsize _legacysocket_getOutputBufferSpaceIncludingTCP(LegacySocket* socket) {
    /* get the space in the socket layer */
    gsize space = legacysocket_getOutputBufferSpace(socket);

    /* internal TCP buffers count against our space */
    gsize tcpLength = socket->protocol == PTCP ? tcp_getOutputBufferLength((TCP*)socket) : 0;

    /* subtract tcpLength without underflowing space */
    space = (tcpLength < space) ? (space - tcpLength) : 0;

    return space;
}

// takes ownership of `inetSocket` (will free/drop)
gboolean legacysocket_addToOutputBuffer(LegacySocket* socket, InetSocket* inetSocket,
                                        const Host* host, Packet* packet) {
    MAGIC_ASSERT(socket);

    /* check if the packet fits */
    gsize length = packet_getPayloadSize(packet);
    if(length > legacysocket_getOutputBufferSpace(socket)) {
        return FALSE;
    }

    /* add to our queue */
    if(packet_getPriority(packet) == 0) {
        /* control packets get sent first */
        g_queue_push_tail(socket->outputControlBuffer, packet);
    } else {
        g_queue_push_tail(socket->outputBuffer, packet);
    }

    socket->outputBufferLength += length;
    packet_addDeliveryStatus(packet, PDS_SND_SOCKET_BUFFERED);

    /* update the tracker input buffer stats */
    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL) {
        CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
        tracker_updateSocketOutputBuffer(
            tracker, &compatSocket, socket->outputBufferLength, socket->outputBufferSize);
    }

    /* we just added a packet, we are no longer writable if full */
    if(_legacysocket_getOutputBufferSpaceIncludingTCP(socket) <= 0) {
        legacyfile_adjustStatus((LegacyFile*)socket, STATUS_FILE_WRITABLE, FALSE);
    }

    /* tell the interface to include us when sending out to the network */
    in_addr_t ip = packet_getSourceIP(packet);
    socket_wants_to_send_with_global_cb_queue(host, inetSocket, ip);

    return TRUE;
}

Packet* legacysocket_removeFromOutputBuffer(LegacySocket* socket, const Host* host) {
    MAGIC_ASSERT(socket);

    /* see if we have any packets */
    Packet* packet = !g_queue_is_empty(socket->outputControlBuffer) ?
            g_queue_pop_head(socket->outputControlBuffer) : g_queue_pop_head(socket->outputBuffer);

    if(packet) {
        /* just removed a packet */
        gsize length = packet_getPayloadSize(packet);
        socket->outputBufferLength -= length;

        /* check if we need to reduce the buffer size */
        if(socket->outputBufferSizePending > 0) {
            legacysocket_setOutputBufferSize(socket, socket->outputBufferSizePending);
        }

        /* update the tracker input buffer stats */
        Tracker* tracker = host_getTracker(host);
        if (tracker != NULL) {
            CompatSocket compatSocket = compatsocket_fromLegacySocket(socket);
            tracker_updateSocketOutputBuffer(
                tracker, &compatSocket, socket->outputBufferLength, socket->outputBufferSize);
        }

        /* we are writable if we now have space */
        gsize space = _legacysocket_getOutputBufferSpaceIncludingTCP(socket);
        gboolean is_active = legacyfile_getStatus((LegacyFile*)socket) & STATUS_FILE_ACTIVE;
        if (space > 0 && is_active) {
            legacyfile_adjustStatus((LegacyFile*)socket, STATUS_FILE_WRITABLE, TRUE);
        }
    }

    return packet;
}

gboolean legacysocket_isUnix(LegacySocket* socket) {
    return (socket->flags & SF_UNIX) ? TRUE : FALSE;
}

void legacysocket_setUnix(LegacySocket* socket, gboolean isUnixSocket) {
    MAGIC_ASSERT(socket);
    socket->flags = isUnixSocket ? (socket->flags | SF_UNIX) : (socket->flags & ~SF_UNIX);
}

void legacysocket_setUnixPath(LegacySocket* socket, const gchar* path, gboolean isBound) {
    MAGIC_ASSERT(socket);
    if(isBound) {
        socket->flags |= SF_UNIX_BOUND;
    }
    socket->unixPath = g_strdup(path);
}

gchar* legacysocket_getUnixPath(LegacySocket* socket) {
    MAGIC_ASSERT(socket);
    return socket->unixPath;
}
