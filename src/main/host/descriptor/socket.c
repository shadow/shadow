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
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/transport.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

static Socket* _socket_fromDescriptor(Descriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_TCPSOCKET ||
                   descriptor_getType(descriptor) == DT_UDPSOCKET);
    return (Socket*)descriptor;
}

static void _socket_free(Descriptor* descriptor) {
    Socket* socket = _socket_fromDescriptor(descriptor);
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
    socket->vtable->free((Descriptor*)socket);
}

static gboolean _socket_close(Descriptor* descriptor) {
    Socket* socket = _socket_fromDescriptor(descriptor);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);

    Tracker* tracker = host_getTracker(worker_getActiveHost());
    tracker_removeSocket(tracker, descriptor_getHandle(descriptor));

    return socket->vtable->close((Descriptor*)socket);
}

static gssize _socket_sendUserData(Transport* transport, gconstpointer buffer,
                                   gsize nBytes, in_addr_t ip, in_port_t port) {
    Socket* socket = _socket_fromDescriptor((Descriptor*)transport);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->send((Transport*)socket, buffer, nBytes, ip, port);
}

static gssize _socket_receiveUserData(Transport* transport, gpointer buffer,
                                      gsize nBytes, in_addr_t* ip,
                                      in_port_t* port) {
    Socket* socket = _socket_fromDescriptor((Descriptor*)transport);
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->receive((Transport*)socket, buffer, nBytes, ip, port);
}

TransportFunctionTable socket_functions = {
    _socket_close, _socket_free, _socket_sendUserData, _socket_receiveUserData,
    MAGIC_VALUE};

void socket_init(Socket* socket, SocketFunctionTable* vtable,
                 DescriptorType type, guint receiveBufferSize,
                 guint sendBufferSize) {
    utility_assert(socket && vtable);

    transport_init(&(socket->super), &socket_functions, type);

    MAGIC_INIT(socket);
    MAGIC_INIT(vtable);

    socket->vtable = vtable;

    socket->protocol = type == DT_TCPSOCKET ? PTCP : type == DT_UDPSOCKET ? PUDP : PLOCAL;
    socket->inputBuffer = g_queue_new();
    socket->inputBufferSize = receiveBufferSize;
    socket->outputBuffer = g_queue_new();
    socket->outputControlBuffer = g_queue_new();
    socket->outputBufferSize = sendBufferSize;

    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Descriptor* descriptor = (Descriptor *)socket;
    tracker_addSocket(tracker, descriptor->handle, socket->protocol, socket->inputBufferSize, socket->outputBufferSize);
}

ProtocolType socket_getProtocol(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->protocol;
}

/* interface functions, implemented by subtypes */

gboolean socket_isFamilySupported(Socket* socket, sa_family_t family) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    return socket->vtable->isFamilySupported(socket, family);
}

gint socket_connectToPeer(Socket* socket, in_addr_t ip, in_port_t port, sa_family_t family) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);

    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Descriptor* descriptor = (Descriptor *)socket;
    tracker_updateSocketPeer(tracker, descriptor->handle, ip, ntohs(port));

    return socket->vtable->connectToPeer(socket, ip, port, family);
}

void socket_pushInPacket(Socket* socket, Packet* packet) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_PROCESSED);
    socket->vtable->process(socket, packet);
}

void socket_dropPacket(Socket* socket, Packet* packet) {
    MAGIC_ASSERT(socket);
    MAGIC_ASSERT(socket->vtable);
    socket->vtable->dropPacket(socket, packet);
}

/* functions implemented by socket */

Packet* socket_pullOutPacket(Socket* socket) {
    return socket_removeFromOutputBuffer(socket);
}

Packet* socket_peekNextPacket(const Socket* socket) {
    MAGIC_ASSERT(socket);
    if(!g_queue_is_empty(socket->outputControlBuffer)) {
        return g_queue_peek_head(socket->outputControlBuffer);
    } else {
        return g_queue_peek_head(socket->outputBuffer);
    }
}

gboolean socket_getPeerName(Socket* socket, in_addr_t* ip, in_port_t* port) {
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

void socket_setPeerName(Socket* socket, in_addr_t ip, in_port_t port) {
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

gboolean socket_getSocketName(Socket* socket, in_addr_t* ip, in_port_t* port) {
    MAGIC_ASSERT(socket);

    /* boundAddress could be 0 (INADDR_NONE), so just check port */
    if(!socket_isBound(socket)) {
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

void socket_setSocketName(Socket* socket, in_addr_t ip, in_port_t port) {
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
    g_string_append_printf(stringBuffer, ":%u (descriptor %i)", ntohs(port), socket->super.super.handle);
    socket->boundString = g_string_free(stringBuffer, FALSE);

    /* the socket is now bound */
    socket->flags |= SF_BOUND;
}

gboolean socket_isBound(Socket* socket) {
    MAGIC_ASSERT(socket);
    return (socket->flags & SF_BOUND) ? TRUE : FALSE;
}

gsize socket_getInputBufferSpace(Socket* socket) {
    MAGIC_ASSERT(socket);
    utility_assert(socket->inputBufferSize >= socket->inputBufferLength);
    gsize bufferSize = socket_getInputBufferSize(socket);
    if(bufferSize < socket->inputBufferLength) {
        return 0;
    } else {
        return bufferSize - socket->inputBufferLength;
    }
}

gsize socket_getOutputBufferSpace(Socket* socket) {
    MAGIC_ASSERT(socket);
    utility_assert(socket->outputBufferSize >= socket->outputBufferLength);
    gsize bufferSize = socket_getOutputBufferSize(socket);
    if(bufferSize < socket->outputBufferLength) {
        return 0;
    } else {
        return bufferSize - socket->outputBufferLength;
    }
}

gsize socket_getInputBufferLength(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->inputBufferLength;
}

gsize socket_getOutputBufferLength(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->outputBufferLength;
}

gsize socket_getInputBufferSize(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->inputBufferSizePending > 0 ? socket->inputBufferSizePending : socket->inputBufferSize;
}

gsize socket_getOutputBufferSize(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->outputBufferSizePending > 0 ? socket->outputBufferSizePending : socket->outputBufferSize;
}

void socket_setInputBufferSize(Socket* socket, gsize newSize) {
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

void socket_setOutputBufferSize(Socket* socket, gsize newSize) {
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

gboolean socket_addToInputBuffer(Socket* socket, Packet* packet) {
    MAGIC_ASSERT(socket);

    /* check if the packet fits */
    guint length = packet_getPayloadLength(packet);
    if(length > socket_getInputBufferSpace(socket)) {
        return FALSE;
    }

    /* add to our queue */
    g_queue_push_tail(socket->inputBuffer, packet);
    packet_ref(packet);
    socket->inputBufferLength += length;
    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_BUFFERED);

    /* update the tracker input buffer stats */
    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Descriptor* descriptor = (Descriptor *)socket;
    tracker_updateSocketInputBuffer(tracker, descriptor->handle, socket->inputBufferLength, socket->inputBufferSize);

    /* we just added a packet, so we are readable */
    if(socket->inputBufferLength > 0) {
        descriptor_adjustStatus((Descriptor*)socket, STATUS_DESCRIPTOR_READABLE, TRUE);
    }

    return TRUE;
}

Packet* socket_removeFromInputBuffer(Socket* socket) {
    MAGIC_ASSERT(socket);

    /* see if we have any packets */
    Packet* packet = g_queue_pop_head(socket->inputBuffer);
    if(packet) {
        /* just removed a packet */
        guint length = packet_getPayloadLength(packet);
        socket->inputBufferLength -= length;

        /* check if we need to reduce the buffer size */
        if(socket->inputBufferSizePending > 0) {
            socket_setInputBufferSize(socket, socket->inputBufferSizePending);
        }

        /* update the tracker input buffer stats */
        Tracker* tracker = host_getTracker(worker_getActiveHost());
        Descriptor* descriptor = (Descriptor *)socket;
        tracker_updateSocketInputBuffer(tracker, descriptor->handle, socket->inputBufferLength, socket->inputBufferSize);

        /* we are not readable if we are now empty */
        if(socket->inputBufferLength <= 0) {
            descriptor_adjustStatus((Descriptor*)socket, STATUS_DESCRIPTOR_READABLE, FALSE);
        }
    }

    return packet;
}

gsize _socket_getOutputBufferSpaceIncludingTCP(Socket* socket) {
    /* get the space in the socket layer */
    gsize space = socket_getOutputBufferSpace(socket);

    /* internal TCP buffers count against our space */
    gsize tcpLength = socket->protocol == PTCP ? tcp_getOutputBufferLength((TCP*)socket) : 0;

    /* subtract tcpLength without underflowing space */
    space = (tcpLength < space) ? (space - tcpLength) : 0;

    return space;
}

gboolean socket_addToOutputBuffer(Socket* socket, Packet* packet) {
    MAGIC_ASSERT(socket);

    /* check if the packet fits */
    guint length = packet_getPayloadLength(packet);
    if(length > socket_getOutputBufferSpace(socket)) {
        return FALSE;
    }

    /* add to our queue */
    if(packet_getPriority(packet) == 0.0f) {
        /* control packets get sent first */
        g_queue_push_tail(socket->outputControlBuffer, packet);
    } else {
        g_queue_push_tail(socket->outputBuffer, packet);
    }

    socket->outputBufferLength += length;
    packet_addDeliveryStatus(packet, PDS_SND_SOCKET_BUFFERED);

    /* update the tracker input buffer stats */
    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Descriptor* descriptor = (Descriptor *)socket;
    tracker_updateSocketOutputBuffer(tracker, descriptor->handle, socket->outputBufferLength, socket->outputBufferSize);

    /* we just added a packet, we are no longer writable if full */
    if(_socket_getOutputBufferSpaceIncludingTCP(socket) <= 0) {
        descriptor_adjustStatus((Descriptor*)socket, STATUS_DESCRIPTOR_WRITABLE, FALSE);
    }

    /* tell the interface to include us when sending out to the network */
    in_addr_t ip = packet_getSourceIP(packet);
    NetworkInterface* interface = host_lookupInterface(worker_getActiveHost(), ip);
    networkinterface_wantsSend(interface, socket);

    return TRUE;
}

Packet* socket_removeFromOutputBuffer(Socket* socket) {
    MAGIC_ASSERT(socket);

    /* see if we have any packets */
    Packet* packet = !g_queue_is_empty(socket->outputControlBuffer) ?
            g_queue_pop_head(socket->outputControlBuffer) : g_queue_pop_head(socket->outputBuffer);

    if(packet) {
        /* just removed a packet */
        guint length = packet_getPayloadLength(packet);
        socket->outputBufferLength -= length;

        /* check if we need to reduce the buffer size */
        if(socket->outputBufferSizePending > 0) {
            socket_setOutputBufferSize(socket, socket->outputBufferSizePending);
        }

        /* update the tracker input buffer stats */
        Tracker* tracker = host_getTracker(worker_getActiveHost());
        Descriptor* descriptor = (Descriptor *)socket;
        tracker_updateSocketOutputBuffer(tracker, descriptor->handle, socket->outputBufferLength, socket->outputBufferSize);

        /* we are writable if we now have space */
        if(_socket_getOutputBufferSpaceIncludingTCP(socket) > 0) {
            descriptor_adjustStatus((Descriptor*)socket, STATUS_DESCRIPTOR_WRITABLE, TRUE);
        }
    }

    return packet;
}

gboolean socket_isUnix(Socket* socket) {
    return (socket->flags & SF_UNIX) ? TRUE : FALSE;
}

void socket_setUnix(Socket* socket, gboolean isUnixSocket) {
    MAGIC_ASSERT(socket);
    socket->flags = isUnixSocket ? (socket->flags | SF_UNIX) : (socket->flags & ~SF_UNIX);
}

void socket_setUnixPath(Socket* socket, const gchar* path, gboolean isBound) {
    MAGIC_ASSERT(socket);
    if(isBound) {
        socket->flags |= SF_UNIX_BOUND;
    }
    socket->unixPath = g_strdup(path);
}

gchar* socket_getUnixPath(Socket* socket) {
    MAGIC_ASSERT(socket);
    return socket->unixPath;
}
