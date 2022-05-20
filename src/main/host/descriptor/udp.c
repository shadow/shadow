/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/udp.h"

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "lib/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/transport.h"
#include "main/host/host.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

enum UDPState {
    UDPS_CLOSED, UDPS_ESTABLISHED,
};

static const gchar* _udpStateStrings[] = {
    "UDPS_CLOSED", "UDPS_ESTABLISHED",
};

static const gchar* _udp_stateToAscii(enum UDPState state) {
    return _udpStateStrings[state];
}

struct _UDP {
    LegacySocket super;
    enum UDPState state;
    enum UDPState stateLast;

    MAGIC_DECLARE;
};

static void _udp_setState(UDP* udp, enum UDPState state) {
    MAGIC_ASSERT(udp);

    udp->stateLast = udp->state;
    udp->state = state;

    trace("%s <-> %s: moved from UDP state '%s' to '%s'", udp->super.boundString, udp->super.peerString,
            _udp_stateToAscii(udp->stateLast), _udp_stateToAscii(udp->state));
}

static UDP* _udp_fromLegacyDescriptor(LegacyDescriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_UDPSOCKET);
    return (UDP*)descriptor;
}

static gboolean _udp_isFamilySupported(LegacySocket* socket, sa_family_t family) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)socket);
    MAGIC_ASSERT(udp);
    return (family == AF_INET || family == AF_UNSPEC || family == AF_UNIX) ? TRUE : FALSE;
}

static gint _udp_connectToPeer(LegacySocket* socket, Host* host, in_addr_t ip, in_port_t port,
                               sa_family_t family) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)socket);
    MAGIC_ASSERT(udp);


    /* ip/port specifies the default destination for packets */
    if(family == AF_UNSPEC) {
        /* dissolve our existing defaults */
        legacysocket_setPeerName(&(udp->super), 0, 0);
        _udp_setState(udp, UDPS_CLOSED);
    } else {
        /* set new defaults */
        legacysocket_setPeerName(&(udp->super), ip, port);
        _udp_setState(udp, UDPS_ESTABLISHED);
    }

    return 0;
}

static void _udp_processPacket(LegacySocket* socket, Host* host, Packet* packet) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)socket);
    MAGIC_ASSERT(udp);

    /* UDP packet can be buffered immediately */
    if (!legacysocket_addToInputBuffer((LegacySocket*)udp, host, packet)) {
        packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
    }
}

static void _udp_dropPacket(LegacySocket* socket, Host* host, Packet* packet) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)socket);
    MAGIC_ASSERT(udp);

    /* do nothing */
}

/*
 * this function builds a UDP packet and sends to the virtual node given by the
 * ip and port parameters. this function assumes that the socket is already
 * bound to a local port, no matter if that happened explicitly or implicitly.
 */
static gssize _udp_sendUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                                gsize nBytes, in_addr_t ip, in_port_t port) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)transport);
    MAGIC_ASSERT(udp);

    const gsize maxPacketLength = CONFIG_DATAGRAM_MAX_SIZE;
    if (nBytes > maxPacketLength) {
        return -EMSGSIZE;
    }

    gsize space = legacysocket_getOutputBufferSpace(&(udp->super));
    if(space < nBytes) {
        /* not enough space to buffer the data */
        return -EWOULDBLOCK;
    }

    /* use default destination if none was specified */
    in_addr_t destinationIP = (ip != 0) ? ip : udp->super.peerIP;
    in_port_t destinationPort = (port != 0) ? port : udp->super.peerPort;

    in_addr_t sourceIP = 0;
    in_port_t sourcePort = 0;
    legacysocket_getSocketName(&(udp->super), &sourceIP, &sourcePort);

    Host* host = thread_getHost(thread);
    if (sourceIP == htonl(INADDR_ANY)) {
        /* source interface depends on destination */
        if (destinationIP == htonl(INADDR_LOOPBACK)) {
            sourceIP = htonl(INADDR_LOOPBACK);
        } else {
            sourceIP = host_getDefaultIP(host);
        }
    }

    utility_assert(sourceIP && sourcePort && destinationIP && destinationPort);

    /* create the UDP packet */
    Packet* packet = packet_new(host);
    packet_setPayload(packet, thread, buffer, nBytes);
    packet_setUDP(packet, PUDP_NONE, sourceIP, sourcePort, destinationIP, destinationPort);
    packet_addDeliveryStatus(packet, PDS_SND_CREATED);

    /* buffer it in the transport layer, to be sent out when possible */
    gboolean success = legacysocket_addToOutputBuffer((LegacySocket*)udp, host, packet);

    gsize bytes_sent = 0;
    /* counter maintenance */
    if (success) {
        bytes_sent = nBytes;
    } else {
        warning("unable to send UDP packet");
    }

    trace("buffered %"G_GSIZE_FORMAT" outbound UDP bytes from user", bytes_sent);

    // return EWOULDBLOCK only if no bytes were sent, and we were requested to send >0 bytes
    if(bytes_sent == 0 && nBytes > 0) {
        return -EWOULDBLOCK;
    }

    return bytes_sent;
}

static gssize _udp_receiveUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                                   gsize nBytes, in_addr_t* ip, in_port_t* port) {
    UDP* udp = _udp_fromLegacyDescriptor((LegacyDescriptor*)transport);
    MAGIC_ASSERT(udp);

    if (legacysocket_peekNextInPacket(&(udp->super)) == NULL) {
        return -EWOULDBLOCK;
    }

    if (buffer.val == 0 && nBytes > 0) {
        return -EFAULT;
    }

    const Packet* nextPacket = legacysocket_peekNextInPacket((LegacySocket*)udp);
    if (!nextPacket) {
        return -EWOULDBLOCK;
    }

    /* copy lesser of requested and available amount to application buffer */
    guint packetLength = packet_getPayloadLength(nextPacket);
    gsize copyLength = MIN(nBytes, packetLength);
    gssize bytesCopied = packet_copyPayload(nextPacket, thread, 0, buffer, copyLength);
    if (bytesCopied < 0) {
        // Error writing to PluginVirtualPtr
        return bytesCopied;
    }

    Packet* packet = legacysocket_removeFromInputBuffer((LegacySocket*)udp, thread_getHost(thread));

    utility_assert(bytesCopied == copyLength);
    packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DELIVERED);

    /* fill in address info */
    if(ip) {
        *ip = packet_getSourceIP(packet);
    }
    if(port) {
        *port = packet_getSourcePort(packet);
    }

    /* destroy packet, throwing away any bytes not claimed by the app */
    packet_unref(packet);

    trace("user read %ld inbound UDP bytes", bytesCopied);

    return bytesCopied;
}

static void _udp_free(LegacyDescriptor* descriptor) {
    UDP* udp = _udp_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(udp);

    descriptor_clear(descriptor);
    MAGIC_CLEAR(udp);
    g_free(udp);

    worker_count_deallocation(UDP);
}

static void _udp_close(LegacyDescriptor* descriptor, Host* host) {
    UDP* udp = _udp_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(udp);
    _udp_setState(udp, UDPS_CLOSED);

    CompatSocket compat_socket = compatsocket_fromLegacySocket(&udp->super);
    host_disassociateInterface(host, &compat_socket);
}

gint udp_shutdown(UDP* udp, gint how) {
    MAGIC_ASSERT(udp);

    if(udp->state == UDPS_CLOSED) {
        return -ENOTCONN;
    }

    return 0;
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable udp_functions = {_udp_close,
                                     NULL,
                                     _udp_free,
                                     _udp_sendUserData,
                                     _udp_receiveUserData,
                                     _udp_processPacket,
                                     _udp_isFamilySupported,
                                     _udp_connectToPeer,
                                     _udp_dropPacket,
                                     MAGIC_VALUE};

UDP* udp_new(Host* host, guint receiveBufferSize, guint sendBufferSize) {
    UDP* udp = g_new0(UDP, 1);
    MAGIC_INIT(udp);

    legacysocket_init(
        &(udp->super), host, &udp_functions, DT_UDPSOCKET, receiveBufferSize, sendBufferSize);

    udp->state = UDPS_CLOSED;
    udp->stateLast = UDPS_CLOSED;

    /* we are immediately active because UDP doesnt wait for accept or connect */
    descriptor_adjustStatus(
        (LegacyDescriptor*)udp, STATUS_DESCRIPTOR_ACTIVE | STATUS_DESCRIPTOR_WRITABLE, TRUE);

    worker_count_allocation(UDP);

    return udp;
}
