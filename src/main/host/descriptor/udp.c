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

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/transport.h"
#include "main/host/host.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _UDP {
    Socket super;

    MAGIC_DECLARE;
};

gboolean udp_isFamilySupported(UDP* udp, sa_family_t family) {
    MAGIC_ASSERT(udp);
    return (family == AF_INET || family == AF_UNSPEC || family == AF_UNIX) ? TRUE : FALSE;
}

gint udp_connectToPeer(UDP* udp, in_addr_t ip, in_port_t port, sa_family_t family) {
    MAGIC_ASSERT(udp);


    /* ip/port specifies the default destination for packets */
    if(family == AF_UNSPEC) {
        /* dissolve our existing defaults */
        socket_setPeerName(&(udp->super), 0, 0);
    } else {
        /* set new defaults */
        socket_setPeerName(&(udp->super), ip, port);
    }

    return 0;
}

void udp_processPacket(UDP* udp, Packet* packet) {
    MAGIC_ASSERT(udp);

    /* UDP packet contains data for user and can be buffered immediately */
    if(packet_getPayloadLength(packet) > 0) {
        if(!socket_addToInputBuffer((Socket*)udp, packet)) {
            packet_addDeliveryStatus(packet, PDS_RCV_SOCKET_DROPPED);
        }
    }
}

void udp_dropPacket(UDP* udp, Packet* packet) {
    MAGIC_ASSERT(udp);

    /* do nothing */
}

/*
 * this function builds a UDP packet and sends to the virtual node given by the
 * ip and port parameters. this function assumes that the socket is already
 * bound to a local port, no matter if that happened explicitly or implicitly.
 */
gssize udp_sendUserData(UDP* udp, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(udp);

    gsize space = socket_getOutputBufferSpace(&(udp->super));
    if(space < nBytes) {
        /* not enough space to buffer the data */
        return -EWOULDBLOCK;
    }

    /* break data into segments and send each in a packet */
    gsize maxPacketLength = CONFIG_DATAGRAM_MAX_SIZE;
    gsize remaining = nBytes;
    gsize offset = 0;

    /* create as many packets as needed */
    while(remaining > 0) {
        gsize copyLength = MIN(maxPacketLength, remaining);

        /* use default destination if none was specified */
        in_addr_t destinationIP = (ip != 0) ? ip : udp->super.peerIP;
        in_port_t destinationPort = (port != 0) ? port : udp->super.peerPort;

        in_addr_t sourceIP = 0;
        in_port_t sourcePort = 0;
        socket_getSocketName(&(udp->super), &sourceIP, &sourcePort);

        if(sourceIP == htonl(INADDR_ANY)) {
            /* source interface depends on destination */
            if(destinationIP == htonl(INADDR_LOOPBACK)) {
                sourceIP = htonl(INADDR_LOOPBACK);
            } else {
                sourceIP = host_getDefaultIP(worker_getActiveHost());
            }
        }

        utility_assert(sourceIP && sourcePort && destinationIP && destinationPort);

        /* create the UDP packet */
        Host* host = worker_getActiveHost();
        Packet* packet = packet_new(buffer + offset, copyLength, (guint)host_getID(host), host_getNewPacketID(host));
        packet_setUDP(packet, PUDP_NONE, sourceIP, sourcePort, destinationIP, destinationPort);
        packet_addDeliveryStatus(packet, PDS_SND_CREATED);

        /* buffer it in the transport layer, to be sent out when possible */
        gboolean success = socket_addToOutputBuffer((Socket*) udp, packet);

        /* counter maintenance */
        if(success) {
            remaining -= copyLength;
            offset += copyLength;
        } else {
            warning("unable to send UDP packet");
            break;
        }
    }

    /* update the tracker output buffer stats */
    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Socket* socket = (Socket* )udp;
    Descriptor* descriptor = (Descriptor *)socket;
    gsize outLength = socket_getOutputBufferLength(socket);
    gsize outSize = socket_getOutputBufferSize(socket);
    tracker_updateSocketOutputBuffer(tracker, descriptor->handle, outLength, outSize);

    debug("buffered %"G_GSIZE_FORMAT" outbound UDP bytes from user", offset);

    return offset > 0 ? (gssize) offset : -EWOULDBLOCK;
}

gssize udp_receiveUserData(UDP* udp, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
    MAGIC_ASSERT(udp);

    Packet* packet = socket_removeFromInputBuffer((Socket*)udp);
    if(!packet) {
        return -EWOULDBLOCK;
    }

    /* copy lesser of requested and available amount to application buffer */
    guint packetLength = packet_getPayloadLength(packet);
    gsize copyLength = MIN(nBytes, packetLength);
    guint bytesCopied = packet_copyPayload(packet, 0, buffer, copyLength);

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

    /* update the tracker output buffer stats */
    Tracker* tracker = host_getTracker(worker_getActiveHost());
    Socket* socket = (Socket* )udp;
    Descriptor* descriptor = (Descriptor *)socket;
    gsize outLength = socket_getOutputBufferLength(socket);
    gsize outSize = socket_getOutputBufferSize(socket);
    tracker_updateSocketOutputBuffer(tracker, descriptor->handle, outLength, outSize);

    debug("user read %u inbound UDP bytes", bytesCopied);

    return bytesCopied > 0 ? (gssize)bytesCopied : EWOULDBLOCK;
}

void udp_free(UDP* udp) {
    MAGIC_ASSERT(udp);

    worker_countObject(OBJECT_TYPE_UDP, COUNTER_TYPE_FREE);

    MAGIC_CLEAR(udp);
    g_free(udp);
}

void udp_close(UDP* udp) {
    MAGIC_ASSERT(udp);
    host_closeDescriptor(worker_getActiveHost(), udp->super.super.super.handle);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable udp_functions = {
    (DescriptorFunc) udp_close,
    (DescriptorFunc) udp_free,
    (TransportSendFunc) udp_sendUserData,
    (TransportReceiveFunc) udp_receiveUserData,
    (SocketProcessFunc) udp_processPacket,
    (SocketIsFamilySupportedFunc) udp_isFamilySupported,
    (SocketConnectToPeerFunc) udp_connectToPeer,
    (SocketDropFunc) udp_dropPacket,
    MAGIC_VALUE
};

UDP* udp_new(gint handle, guint receiveBufferSize, guint sendBufferSize) {
    UDP* udp = g_new0(UDP, 1);
    MAGIC_INIT(udp);

    socket_init(&(udp->super), &udp_functions, DT_UDPSOCKET, handle, receiveBufferSize, sendBufferSize);

    /* we are immediately active because UDP doesnt wait for accept or connect */
    descriptor_adjustStatus((Descriptor*) udp, DS_ACTIVE|DS_WRITABLE, TRUE);

    worker_countObject(OBJECT_TYPE_UDP, COUNTER_TYPE_NEW);

    return udp;
}
