/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <stddef.h>

#include "main/core/support/definitions.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/host/network_queuing_disciplines.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/utility/pcap_writer.h"
#include "main/utility/priority_queue.h"
#include "main/utility/tagged_ptr.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef struct _NetworkInterfaceTokenBucket NetworkInterfaceTokenBucket;
struct _NetworkInterfaceTokenBucket {
    /* The maximum number of bytes the bucket can hold */
    guint64 bytesCapacity;
    /* The number of bytes remaining in the bucket */
    guint64 bytesRemaining;
    /* The number of bytes that get added to the bucket every millisecond */
    guint64 bytesRefill;
};

struct _NetworkInterface {
    /* The upstream ISP router connected to this interface.
     * May be NULL for loopback interfaces. */
    Router* router;

    /* The queuing discipline used by this interface to schedule the
     * sending of packets from sockets. */
    QDiscMode qdisc;

    /* The address associated with this interface */
    Address* address;

    /* (protocol,port)-to-socket bindings. Stores CompatSocket objects as tagged pointers. */
    GHashTable* boundSockets;

    /* Transports wanting to send data out. */
    RrSocketQueue rrQueue;
    FifoSocketQueue fifoQueue;

    /* the outgoing token bucket implements traffic shaping, i.e.,
     * packets are delayed until they conform with outgoing rate limits.*/
    NetworkInterfaceTokenBucket sendBucket;

    /* the incoming token bucket implements traffic policing, i.e.,
     * packets that do not conform to incoming rate limits are dropped. */
    NetworkInterfaceTokenBucket receiveBucket;

    /* Store the time we started refilling our token buckets. This is used to
     * help us compute when refills should occur following an idle period. */
    SimulationTime timeStartedRefillingBuckets;

    /* If we have scheduled a refill task but it has not yet executed. */
    gboolean isRefillPending;

    /* To support capturing incoming and outgoing packets */
    PCapWriter* pcap;

    MAGIC_DECLARE;
};

/* forward declarations */
static void _networkinterface_sendPackets(NetworkInterface* interface);
static void _networkinterface_refillTokenBucketsCB(NetworkInterface* interface,
                                                   gpointer userData);

static void _compatsocket_unrefTaggedVoid(void* taggedSocketPtr) {
    utility_assert(taggedSocketPtr != NULL);
    if (taggedSocketPtr == NULL) {
        return;
    }

    uintptr_t taggedSocket = (uintptr_t)taggedSocketPtr;
    CompatSocket socket = compatsocket_fromTagged(taggedSocket);
    compatsocket_unref(&socket);
}

static inline SimulationTime _networkinterface_getRefillInterval() {
    return (SimulationTime) SIMTIME_ONE_MILLISECOND*1;
}

static inline guint64 _networkinterface_getCapacityFactor() {
    /* the capacity is this much times the refill rate */
    return (guint64) 1;
}

static void _networkinterface_refillTokenBucket(NetworkInterfaceTokenBucket* bucket) {
    /* We have room to add more tokens. */
    bucket->bytesRemaining += bucket->bytesRefill;
    /* Make sure we stay within capacity. */
    if(bucket->bytesRemaining > bucket->bytesCapacity) {
        bucket->bytesRemaining = bucket->bytesCapacity;
    }
}

static void
_networkinterface_consumeTokenBucket(NetworkInterfaceTokenBucket* bucket,
                                     guint64 bytesConsumed) {
    if (bytesConsumed >= bucket->bytesRemaining) {
        bucket->bytesRemaining = 0;
    } else {
        bucket->bytesRemaining -= bytesConsumed;
    }
}

static void _networkinterface_scheduleRefillTask(NetworkInterface* interface,
                                                 TaskCallbackFunc func,
                                                 SimulationTime delay) {
    Task* refillTask = task_new(func, interface, NULL, NULL, NULL);
    worker_scheduleTask(refillTask, delay);
    task_unref(refillTask);
    interface->isRefillPending = TRUE;
}

static void _networkinterface_scheduleNextRefill(NetworkInterface* interface) {
    SimulationTime now = worker_getCurrentTime();
    SimulationTime interval = _networkinterface_getRefillInterval();

    /* This computes the time that the next refill should have occurred if we
     * were to always call the refill function every interval, even though in
     * practice we stop scheduling refill tasks once we notice we are idle. */
    SimulationTime offset = now - interface->timeStartedRefillingBuckets;
    SimulationTime relTimeSincelastRefill = offset % interval;
    SimulationTime relTimeUntilNextRefill = interval - relTimeSincelastRefill;

    /* call back when we need the next refill */
    _networkinterface_scheduleRefillTask(
        interface, (TaskCallbackFunc)_networkinterface_refillTokenBucketsCB,
        relTimeUntilNextRefill);
}

static gboolean _networkinterface_isRefillNeeded(NetworkInterface* interface) {
    gboolean sendNeedsRefill = interface->sendBucket.bytesRemaining <
                               interface->sendBucket.bytesCapacity;
    gboolean receiveNeedsRefill = interface->receiveBucket.bytesRemaining <
                                  interface->receiveBucket.bytesCapacity;
    return (sendNeedsRefill || receiveNeedsRefill) &&
           !interface->isRefillPending;
}

static void
_networkinterface_scheduleNextRefillIfNeeded(NetworkInterface* interface) {
    if (_networkinterface_isRefillNeeded(interface)) {
        _networkinterface_scheduleNextRefill(interface);
    }
}

static void _networkinterface_refillTokenBucketsCB(NetworkInterface* interface,
                                                   gpointer userData) {
    MAGIC_ASSERT(interface);

    /* We no longer have an outstanding event in the event queue. */
    interface->isRefillPending = FALSE;

    /* Refill the token buckets. */
    _networkinterface_refillTokenBucket(&interface->receiveBucket);
    _networkinterface_refillTokenBucket(&interface->sendBucket);

    /* the refill may have caused us to be able to receive and send again.
     * we only receive packets from an upstream router if we have one (i.e.,
     * if this is not a loopback interface). */
    if(interface->router) {
        networkinterface_receivePackets(interface);
    }
    _networkinterface_sendPackets(interface);

    _networkinterface_scheduleNextRefillIfNeeded(interface);
}

void networkinterface_startRefillingTokenBuckets(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    interface->timeStartedRefillingBuckets = worker_getCurrentTime();
    _networkinterface_refillTokenBucketsCB(interface, NULL);
}

static void _networkinterface_setupTokenBuckets(NetworkInterface* interface,
        guint64 bwDownKiBps, guint64 bwUpKiBps) {
    MAGIC_ASSERT(interface);

    /* set up the token buckets */
    g_assert(_networkinterface_getRefillInterval() <= SIMTIME_ONE_SECOND);

    SimulationTime timeFactor =
        SIMTIME_ONE_SECOND / _networkinterface_getRefillInterval();
    guint64 bytesPerIntervalSend = bwUpKiBps * 1024 / timeFactor;
    guint64 bytesPerIntervalReceive = bwDownKiBps * 1024 / timeFactor;

    interface->receiveBucket.bytesRefill = bytesPerIntervalReceive;
    interface->sendBucket.bytesRefill = bytesPerIntervalSend;

    /* the CONFIG_MTU parts make sure we don't lose any partial bytes we had left
     * from last round when we do the refill. */

    guint64 capacityFactor = _networkinterface_getCapacityFactor();
    interface->sendBucket.bytesCapacity =
        (interface->sendBucket.bytesRefill * capacityFactor) + CONFIG_MTU;
    interface->receiveBucket.bytesCapacity =
        (interface->receiveBucket.bytesRefill * capacityFactor) + CONFIG_MTU;

    info("interface %s token buckets can send %" G_GUINT64_FORMAT " bytes "
         "every %" G_GUINT64_FORMAT " nanoseconds",
         address_toString(interface->address),
         interface->sendBucket.bytesRefill,
         _networkinterface_getRefillInterval());
    info("interface %s token buckets can receive %" G_GUINT64_FORMAT " bytes "
         "every %" G_GUINT64_FORMAT " nanoseconds",
         address_toString(interface->address),
         interface->receiveBucket.bytesRefill,
         _networkinterface_getRefillInterval());
}

Address* networkinterface_getAddress(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->address;
}

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    SimulationTime timeFactor =
        SIMTIME_ONE_SECOND / _networkinterface_getRefillInterval();
    guint64 bytesPerSecond = ((guint64)interface->sendBucket.bytesRefill) * ((guint64)timeFactor);
    guint64 kibPerSecond = bytesPerSecond / 1024;

    return (guint32)kibPerSecond;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    SimulationTime timeFactor =
        SIMTIME_ONE_SECOND / _networkinterface_getRefillInterval();
    guint64 bytesPerSecond = ((guint64)interface->receiveBucket.bytesRefill) * ((guint64)timeFactor);
    guint64 kibPerSecond = bytesPerSecond / 1024;

    return (guint32)kibPerSecond;
}

static gchar* _networkinterface_getAssociationKey(NetworkInterface* interface,
        ProtocolType type, in_port_t port, in_addr_t peerAddr, in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    GString* strBuffer = g_string_new(NULL);
    g_string_printf(strBuffer,
            "%s|%"G_GUINT32_FORMAT":%"G_GUINT16_FORMAT"|%"G_GUINT32_FORMAT":%"G_GUINT16_FORMAT,
            protocol_toString(type),
            (guint)address_toNetworkIP(interface->address),
            port, peerAddr, peerPort);

    return g_string_free(strBuffer, FALSE);
}

static gchar* _networkinterface_socketToAssociationKey(NetworkInterface* interface, const CompatSocket* socket) {
    MAGIC_ASSERT(interface);

    ProtocolType type = compatsocket_getProtocol(socket);

    in_addr_t peerIP = 0;
    in_port_t peerPort = 0;
    compatsocket_getPeerName(socket, &peerIP, &peerPort);

    in_addr_t boundIP = 0;
    in_port_t boundPort = 0;
    compatsocket_getSocketName(socket, &boundIP, &boundPort);

    gchar* key = _networkinterface_getAssociationKey(interface, type, boundPort, peerIP, peerPort);
    return key;
}

gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
        in_port_t port, in_addr_t peerAddr, in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    gboolean isFound = FALSE;

    /* we need to check the general key too (ie the ones listening sockets use) */
    gchar* general = _networkinterface_getAssociationKey(interface, type, port, 0, 0);
    if(g_hash_table_contains(interface->boundSockets, general)) {
        isFound = TRUE;
    }
    g_free(general);

    if(!isFound) {
        gchar* specific = _networkinterface_getAssociationKey(interface, type, port, peerAddr, peerPort);
        if(g_hash_table_contains(interface->boundSockets, specific)) {
            isFound = TRUE;
        }
        g_free(specific);
    }

    return isFound;
}

void networkinterface_associate(NetworkInterface* interface, const CompatSocket* socket) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_socketToAssociationKey(interface, socket);

    /* make sure there is no collision */
    utility_assert(!g_hash_table_contains(interface->boundSockets, key));

    /* need to store our own reference to the socket object */
    CompatSocket newSocketRef = compatsocket_refAs(socket);

    /* insert to our storage, key is now owned by table */
    g_hash_table_replace(interface->boundSockets, key, (void*)compatsocket_toTagged(&newSocketRef));

    trace("associated socket key %s", key);
}

void networkinterface_disassociate(NetworkInterface* interface, const CompatSocket* socket) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_socketToAssociationKey(interface, socket);

    /* we will no longer receive packets for this port, this unrefs descriptor */
    g_hash_table_remove(interface->boundSockets, key);

    trace("disassociated socket key %s", key);
    g_free(key);
}

static void _networkinterface_capturePacket(NetworkInterface* interface, Packet* packet) {
    PCapPacket* pcapPacket = g_new0(PCapPacket, 1);

    pcapPacket->headerSize = packet_getHeaderSize(packet);
    pcapPacket->payloadLength = packet_getPayloadLength(packet);

    if(pcapPacket->payloadLength > 0) {
        pcapPacket->payload = g_new0(guchar, pcapPacket->payloadLength);
        packet_copyPayloadShadow(packet, 0, pcapPacket->payload, pcapPacket->payloadLength);
    }

    PacketTCPHeader* tcpHeader = packet_getTCPHeader(packet);

    pcapPacket->srcIP = tcpHeader->sourceIP;
    pcapPacket->dstIP = tcpHeader->destinationIP;
    pcapPacket->srcPort = tcpHeader->sourcePort;
    pcapPacket->dstPort = tcpHeader->destinationPort;

    if(tcpHeader->flags & PTCP_RST) pcapPacket->rstFlag = TRUE;
    if(tcpHeader->flags & PTCP_SYN) pcapPacket->synFlag = TRUE;
    if(tcpHeader->flags & PTCP_ACK) pcapPacket->ackFlag = TRUE;
    if(tcpHeader->flags & PTCP_FIN) pcapPacket->finFlag = TRUE;

    pcapPacket->seq = (guint32)tcpHeader->sequence;
    pcapPacket->win = (guint16)tcpHeader->window;
    if(tcpHeader->flags & PTCP_ACK) {
        pcapPacket->ack = (guint32)tcpHeader->acknowledgment;
    }

    pcapwriter_writePacket(interface->pcap, pcapPacket);

    if(pcapPacket->payloadLength > 0) {
        g_free(pcapPacket->payload);
    }

    g_free(pcapPacket);
}

static CompatSocket _boundsockets_lookup(GHashTable* table, gchar* key) {
    void* ptr = g_hash_table_lookup(table, key);

    if (ptr == NULL) {
        CompatSocket compatSocket = {0};
        compatSocket.type = CST_NONE;
        return compatSocket;
    }

    return compatsocket_fromTagged((uintptr_t)ptr);
}

static void _networkinterface_receivePacket(NetworkInterface* interface, Packet* packet) {
    MAGIC_ASSERT(interface);

    /* get the next packet */
    utility_assert(packet);

    /* successfully received */
    packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_RECEIVED);

    /* hand it off to the correct socket layer */
    ProtocolType ptype = packet_getProtocol(packet);
    in_port_t bindPort = packet_getDestinationPort(packet);

    /* the first check is for servers who don't associate with specific destinations */
    gchar* key = _networkinterface_getAssociationKey(interface, ptype, bindPort, 0, 0);
    trace("looking for socket associated with general key %s", key);

    CompatSocket socket = _boundsockets_lookup(interface->boundSockets, key);
    g_free(key);

    if (socket.type == CST_NONE) {
        /* now check the destination-specific key */
        in_addr_t peerIP = packet_getSourceIP(packet);
        in_port_t peerPort = packet_getSourcePort(packet);

        key = _networkinterface_getAssociationKey(interface, ptype, bindPort, peerIP, peerPort);
        trace("looking for socket associated with specific key %s", key);
        socket = _boundsockets_lookup(interface->boundSockets, key);
        g_free(key);
    }

    /* if the socket closed, just drop the packet */
    if (socket.type != CST_NONE) {
        compatsocket_pushInPacket(&socket, packet);
    } else {
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
    }

    gint socketHandle = -1;
    if (socket.type == CST_LEGACY_SOCKET) {
        socketHandle =
            *descriptor_getHandleReference((LegacyDescriptor*)socket.object.as_legacy_socket);
    }

    /* count our bandwidth usage by interface, and by socket handle if possible */
    tracker_addInputBytes(host_getTracker(worker_getActiveHost()), packet, socketHandle);
    if (interface->pcap) {
        _networkinterface_capturePacket(interface, packet);
    }
}

void networkinterface_receivePackets(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* we can only receive packets from the upstream router if we actually have one.
     * sending on loopback means we don't have a router, sending to a remote host
     * means we do have a router. */
    if(!interface->router) {
        return;
    }

    /* get the bootstrapping mode */
    gboolean bootstrapping = worker_isBootstrapActive();

    while(bootstrapping || interface->receiveBucket.bytesRemaining >= CONFIG_MTU) {
        /* we are now the owner of the packet reference from the router */
        Packet* packet = router_dequeue(interface->router);
        if(!packet) {
            break;
        }

        guint64 length = (guint64)(packet_getPayloadLength(packet) + packet_getHeaderSize(packet));

        _networkinterface_receivePacket(interface, packet);

        /* release reference from router */
        packet_unref(packet);

        /* update bandwidth accounting when not in infinite bandwidth mode */
        if(!bootstrapping) {
            _networkinterface_consumeTokenBucket(&interface->receiveBucket,
                                                 length);
            _networkinterface_scheduleNextRefillIfNeeded(interface);
        }
    }
}

static void _networkinterface_updatePacketHeader(const CompatSocket* socket, Packet* packet) {
    if (socket->type == CST_LEGACY_SOCKET) {
        LegacyDescriptor* descriptor = (LegacyDescriptor*)socket->object.as_legacy_socket;

        LegacyDescriptorType type = descriptor_getType(descriptor);
        if (type == DT_TCPSOCKET) {
            TCP* tcp = (TCP*)descriptor;
            tcp_networkInterfaceIsAboutToSendPacket(tcp, packet);
        }
    }
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface, gint* socketHandle) {
    Packet* packet = NULL;

    while (!packet && !rrsocketqueue_isEmpty(&interface->rrQueue)) {
        /* do round robin to get the next packet from the next socket */
        CompatSocket socket = {0};
        bool found = rrsocketqueue_pop(&interface->rrQueue, &socket);

        if (!found) {
            continue;
        }

        packet = compatsocket_pullOutPacket(&socket);
        if (socket.type == CST_LEGACY_SOCKET) {
            *socketHandle =
                *descriptor_getHandleReference((LegacyDescriptor*)socket.object.as_legacy_socket);
        }

        if (packet) {
            _networkinterface_updatePacketHeader(&socket, packet);
        }

        if (compatsocket_peekNextOutPacket(&socket)) {
            /* socket has more packets, and is still reffed from before */
            rrsocketqueue_push(&interface->rrQueue, &socket);
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            compatsocket_unref(&socket);
        }
    }

    return packet;
}

/* first-in-first-out queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectFirstInFirstOut(NetworkInterface* interface,
                                                       gint* socketHandle) {
    /* use packet priority field to select based on application ordering.
     * this is really a simplification of prioritizing on timestamps. */
    Packet* packet = NULL;

    while (!packet && !fifosocketqueue_isEmpty(&interface->fifoQueue)) {
        /* do fifo to get the next packet from the next socket */
        CompatSocket socket = {0};
        bool found = fifosocketqueue_pop(&interface->fifoQueue, &socket);

        if (!found) {
            continue;
        }

        packet = compatsocket_pullOutPacket(&socket);
        if (socket.type == CST_LEGACY_SOCKET) {
            *socketHandle =
                *descriptor_getHandleReference((LegacyDescriptor*)socket.object.as_legacy_socket);
        }

        if (packet) {
            _networkinterface_updatePacketHeader(&socket, packet);
        }

        if (compatsocket_peekNextOutPacket(&socket)) {
            /* socket has more packets, and is still reffed from before */
            fifosocketqueue_push(&interface->fifoQueue, &socket);
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            compatsocket_unref(&socket);
        }
    }

    return packet;
}

static void _networkinterface_sendPackets(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    gboolean bootstrapping = worker_isBootstrapActive();

    /* loop until we find a socket that has something to send */
    while(interface->sendBucket.bytesRemaining >= CONFIG_MTU) {
        gint socketHandle = -1;

        /* choose which packet to send next based on our queuing discipline */
        Packet* packet;
        switch(interface->qdisc) {
            case Q_DISC_MODE_ROUND_ROBIN: {
                packet = _networkinterface_selectRoundRobin(interface, &socketHandle);
                break;
            }
            case Q_DISC_MODE_FIFO:
            default: {
                packet = _networkinterface_selectFirstInFirstOut(interface, &socketHandle);
                break;
            }
        }
        if(!packet) {
            break;
        }

        packet_addDeliveryStatus(packet, PDS_SND_INTERFACE_SENT);

        /* now actually send the packet somewhere */
        if(address_toNetworkIP(interface->address) == packet_getDestinationIP(packet)) {
            /* packet will arrive on our own interface, so it doesn't need to
             * go through the upstream router and does not consume bandwidth. */
            packet_ref(packet);
            Task* packetTask = task_new((TaskCallbackFunc)_networkinterface_receivePacket,
                    interface, packet, NULL, (TaskArgumentFreeFunc)packet_unref);
            worker_scheduleTask(packetTask, 1);
            task_unref(packetTask);
        } else {
            /* let the upstream router send to remote with appropriate delays.
             * if we get here we are not loopback and should have been assigned a router. */
            utility_assert(interface->router);
            router_forward(interface->router, packet);
        }

        /* successfully sent, calculate how long it took to 'send' this packet */
        if(!bootstrapping) {
            guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
            _networkinterface_consumeTokenBucket(&interface->sendBucket,
                                                 length);
            _networkinterface_scheduleNextRefillIfNeeded(interface);
        }

        tracker_addOutputBytes(host_getTracker(worker_getActiveHost()), packet, socketHandle);
        if(interface->pcap) {
            _networkinterface_capturePacket(interface, packet);
        }

        /* sending side is done with its ref */
        packet_unref(packet);
    }
}

void networkinterface_wantsSend(NetworkInterface* interface, const CompatSocket* socket) {
    MAGIC_ASSERT(interface);

    if (compatsocket_peekNextOutPacket(socket) == NULL) {
        warning("Socket wants send, but no packets available");
        return;
    }

    /* track the new socket for sending if not already tracking */
    switch (interface->qdisc) {
        case Q_DISC_MODE_ROUND_ROBIN: {
            if (!rrsocketqueue_find(&interface->rrQueue, socket)) {
                CompatSocket newSocketRef = compatsocket_refAs(socket);
                rrsocketqueue_push(&interface->rrQueue, &newSocketRef);
            }
            break;
        }
        case Q_DISC_MODE_FIFO:
        default: {
            if (!fifosocketqueue_find(&interface->fifoQueue, socket)) {
                CompatSocket newSocketRef = compatsocket_refAs(socket);
                fifosocketqueue_push(&interface->fifoQueue, &newSocketRef);
            }
            break;
        }
    }

    /* send packets if we can */
    _networkinterface_sendPackets(interface);
}

void networkinterface_setRouter(NetworkInterface* interface, Router* router) {
    MAGIC_ASSERT(interface);
    if(interface->router) {
        router_unref(interface->router);
    }
    interface->router = router;
    if(interface->router) {
        router_ref(interface->router);
    }
}

Router* networkinterface_getRouter(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->router;
}

NetworkInterface* networkinterface_new(Address* address, guint64 bwDownKiBps, guint64 bwUpKiBps,
        gchar* pcapDir, QDiscMode qdisc, guint64 interfaceReceiveLength) {
    NetworkInterface* interface = g_new0(NetworkInterface, 1);
    MAGIC_INIT(interface);

    interface->address = address;
    address_ref(interface->address);

    /* incoming packets get passed along to sockets */
    interface->boundSockets =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, _compatsocket_unrefTaggedVoid);

    /* sockets tell us when they want to start sending */
    rrsocketqueue_init(&interface->rrQueue);
    fifosocketqueue_init(&interface->fifoQueue);

    /* parse queuing discipline */
    interface->qdisc = qdisc;

    if(pcapDir != NULL) {
        GString* filename = g_string_new(NULL);
        g_string_printf(filename, "%s-%s",
                address_toHostName(interface->address),
                address_toHostIPString(interface->address));
        interface->pcap = pcapwriter_new(pcapDir, filename->str);
        g_string_free(filename, TRUE);
    }

    /* set size and refill rates for token buckets */
    _networkinterface_setupTokenBuckets(interface, bwDownKiBps, bwUpKiBps);

    info("bringing up network interface '%s' at '%s', %"G_GUINT64_FORMAT" KiB/s up and %"G_GUINT64_FORMAT" KiB/s down using queuing discipline %s",
            address_toHostName(interface->address), address_toHostIPString(interface->address), bwUpKiBps, bwDownKiBps,
            interface->qdisc == Q_DISC_MODE_ROUND_ROBIN ? "rr" : "fifo");

    worker_count_allocation(NetworkInterface);
    return interface;
}

void networkinterface_free(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* unref all sockets wanting to send */
    rrsocketqueue_destroy(&interface->rrQueue, compatsocket_unref);
    fifosocketqueue_destroy(&interface->fifoQueue, compatsocket_unref);

    g_hash_table_destroy(interface->boundSockets);

    if(interface->router) {
        router_unref(interface->router);
    }

    dns_deregister(worker_getDNS(), interface->address);
    address_unref(interface->address);

    if(interface->pcap) {
        pcapwriter_free(interface->pcap);
    }

    MAGIC_CLEAR(interface);
    g_free(interface);

    worker_count_deallocation(NetworkInterface);
}

