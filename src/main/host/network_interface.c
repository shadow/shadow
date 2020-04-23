/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <stddef.h>

#include "support/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/support/options.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/utility/pcap_writer.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

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

    /* (protocol,port)-to-socket bindings */
    GHashTable* boundSockets;

    /* Transports wanting to send data out */
    GQueue* rrQueue;
    PriorityQueue* fifoQueue;

    /* the outgoing token bucket implements traffic shaping, i.e.,
     * packets are delayed until they conform with outgoing rate limits.*/
    NetworkInterfaceTokenBucket sendBucket;

    /* the incoming token bucket implements traffic policing, i.e.,
     * packets that do not conform to incoming rate limits are dropped. */
    NetworkInterfaceTokenBucket receiveBucket;

    /* To support capturing incoming and outgoing packets */
    PCapWriter* pcap;

    MAGIC_DECLARE;
};

/* forward declaration */
static void _networkinterface_sendPackets(NetworkInterface* interface);

static gint _networkinterface_compareSocket(const Socket* sa, const Socket* sb, gpointer userData) {
    Packet* pa = socket_peekNextPacket(sa);
    Packet* pb = socket_peekNextPacket(sb);
    return packet_getPriority(pa) > packet_getPriority(pb) ? +1 : -1;
}

static inline SimulationTime _networkinterface_getRefillIntervalSend() {
    return (SimulationTime) SIMTIME_ONE_MILLISECOND*1;
}

static inline SimulationTime _networkinterface_getRefillIntervalReceive() {
    return (SimulationTime) SIMTIME_ONE_MILLISECOND*1;
}

static inline guint64 _networkinterface_getCapacityFactorSend() {
    /* the capacity is this much times the refill rate */
    return (guint64) 1;
}

static inline guint64 _networkinterface_getCapacityFactorReceive() {
    /* the capacity is this much times the refill rate */
    return (guint64) 1;
}

static void _networkinterface_refillTokenBucket(NetworkInterfaceTokenBucket* bucket) {
    bucket->bytesRemaining += bucket->bytesRefill;
    if(bucket->bytesRemaining > bucket->bytesCapacity) {
        bucket->bytesRemaining = bucket->bytesCapacity;
    }
}

static inline void _networkinterface_scheduleRefillTask(NetworkInterface* interface,
        TaskCallbackFunc func, SimulationTime delay) {
    Task* refillTask = task_new(func, interface, NULL, NULL, NULL);
    worker_scheduleTask(refillTask, delay);
    task_unref(refillTask);
}

static void _networkinterface_refillTokenBucketsBoth(NetworkInterface* interface, gpointer userData) {
    MAGIC_ASSERT(interface);

    /* add more bytes to the token buckets */
    _networkinterface_refillTokenBucket(&interface->receiveBucket);
    _networkinterface_refillTokenBucket(&interface->sendBucket);

    /* call back when we need the next refill */
    utility_assert(_networkinterface_getRefillIntervalSend() ==
            _networkinterface_getRefillIntervalReceive());

    _networkinterface_scheduleRefillTask(interface,
            (TaskCallbackFunc) _networkinterface_refillTokenBucketsBoth,
            _networkinterface_getRefillIntervalReceive());

    /* the refill may have caused us to be able to receive and send again.
     * we only receive packets from an upstream router if we have one (i.e.,
     * if this is not a loopback interface). */
    if(interface->router) {
        networkinterface_receivePackets(interface);
    }
    _networkinterface_sendPackets(interface);
}

static void _networkinterface_refillTokenBucketSend(NetworkInterface* interface, gpointer userData) {
    MAGIC_ASSERT(interface);

    /* add more bytes to the token buckets */
    _networkinterface_refillTokenBucket(&interface->sendBucket);

    /* call back when we need the next refill */
    _networkinterface_scheduleRefillTask(interface,
            (TaskCallbackFunc) _networkinterface_refillTokenBucketSend,
            _networkinterface_getRefillIntervalSend());

    /* the refill may have caused us to be able to send again */
    _networkinterface_sendPackets(interface);
}

static void _networkinterface_refillTokenBucketReceive(NetworkInterface* interface, gpointer userData) {
    MAGIC_ASSERT(interface);

    /* add more bytes to the token buckets */
    _networkinterface_refillTokenBucket(&interface->receiveBucket);

    /* call back when we need the next refill */
    _networkinterface_scheduleRefillTask(interface,
            (TaskCallbackFunc) _networkinterface_refillTokenBucketReceive,
            _networkinterface_getRefillIntervalReceive());

    /* the refill may have caused us to be able to receive again.
     * we only receive packets from an upstream router if we have one (i.e.,
     * if this is not a loopback interface). */
    if(interface->router) {
        networkinterface_receivePackets(interface);
    }
}

void networkinterface_startRefillingTokenBuckets(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    if(_networkinterface_getRefillIntervalSend() == _networkinterface_getRefillIntervalReceive()) {
        _networkinterface_refillTokenBucketsBoth(interface, NULL);
    } else {
        _networkinterface_refillTokenBucketReceive(interface, NULL);
        _networkinterface_refillTokenBucketSend(interface, NULL);
    }
}

static void _networkinterface_setupTokenBuckets(NetworkInterface* interface,
        guint64 bwDownKiBps, guint64 bwUpKiBps) {
    MAGIC_ASSERT(interface);

    /* set up the token buckets */
    g_assert(_networkinterface_getRefillIntervalSend() <= SIMTIME_ONE_SECOND);
    SimulationTime timeFactorSend = SIMTIME_ONE_SECOND / _networkinterface_getRefillIntervalSend();
    guint64 bytesPerIntervalSend = bwUpKiBps * 1024 / timeFactorSend;

    g_assert(_networkinterface_getRefillIntervalReceive() <= SIMTIME_ONE_SECOND);
    SimulationTime timeFactorReceive = SIMTIME_ONE_SECOND / _networkinterface_getRefillIntervalReceive();
    guint64 bytesPerIntervalReceive = bwDownKiBps * 1024 / timeFactorReceive;

    interface->receiveBucket.bytesRefill = bytesPerIntervalReceive;
    interface->sendBucket.bytesRefill = bytesPerIntervalSend;

    /* the CONFIG_MTU parts make sure we don't lose any partial bytes we had left
     * from last round when we do the refill. */

    guint64 sendFactor = _networkinterface_getCapacityFactorSend();
    interface->sendBucket.bytesCapacity = (interface->sendBucket.bytesRefill * sendFactor) + CONFIG_MTU;

    guint64 receiveFactor = _networkinterface_getCapacityFactorReceive();
    interface->receiveBucket.bytesCapacity = (interface->receiveBucket.bytesRefill * receiveFactor) + CONFIG_MTU;

    info("interface %s token buckets can send %"G_GUINT64_FORMAT" bytes "
            "every %"G_GUINT64_FORMAT" nanoseconds",
            address_toString(interface->address), interface->sendBucket.bytesRefill,
            _networkinterface_getRefillIntervalSend());
    info("interface %s token buckets can receive %"G_GUINT64_FORMAT" bytes "
            "every %"G_GUINT64_FORMAT" nanoseconds",
            address_toString(interface->address), interface->receiveBucket.bytesRefill,
            _networkinterface_getRefillIntervalReceive());
}

Address* networkinterface_getAddress(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->address;
}

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    SimulationTime timeFactor = SIMTIME_ONE_SECOND / _networkinterface_getRefillIntervalSend();
    guint64 bytesPerSecond = ((guint64)interface->sendBucket.bytesRefill) * ((guint64)timeFactor);
    guint64 kibPerSecond = bytesPerSecond / 1024;

    return (guint32)kibPerSecond;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    SimulationTime timeFactor = SIMTIME_ONE_SECOND / _networkinterface_getRefillIntervalReceive();
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

static gchar* _networkinterface_socketToAssociationKey(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    ProtocolType type = socket_getProtocol(socket);

    in_addr_t peerIP = 0;
    in_port_t peerPort = 0;
    socket_getPeerName(socket, &peerIP, &peerPort);

    in_addr_t boundIP = 0;
    in_port_t boundPort = 0;
    socket_getSocketName(socket, &boundIP, &boundPort);

    gchar* key = _networkinterface_getAssociationKey(interface, type, boundPort, peerIP, peerPort);
    return key;
}

gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
        in_port_t port, in_addr_t peerAddr, in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    gboolean isFound = FALSE;

    /* we need to check the general key too (ie the ones listening sockets use) */
    gchar* general = _networkinterface_getAssociationKey(interface, type, port, 0, 0);
    if(g_hash_table_lookup(interface->boundSockets, general)) {
        isFound = TRUE;
    }
    g_free(general);

    if(!isFound) {
        gchar* specific = _networkinterface_getAssociationKey(interface, type, port, peerAddr, peerPort);
        if(g_hash_table_lookup(interface->boundSockets, specific)) {
            isFound = TRUE;
        }
        g_free(specific);
    }

    return isFound;
}

void networkinterface_associate(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_socketToAssociationKey(interface, socket);

    /* make sure there is no collision */
    utility_assert(!g_hash_table_contains(interface->boundSockets, key));

    /* insert to our storage, key is now owned by table */
    g_hash_table_replace(interface->boundSockets, key, socket);
    descriptor_ref(socket);

    debug("associated socket key %s", key);
}

void networkinterface_disassociate(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_socketToAssociationKey(interface, socket);

    /* we will no longer receive packets for this port, this unrefs descriptor */
    g_hash_table_remove(interface->boundSockets, key);

    debug("disassociated socket key %s", key);
    g_free(key);
}

static void _networkinterface_capturePacket(NetworkInterface* interface, Packet* packet) {
    PCapPacket* pcapPacket = g_new0(PCapPacket, 1);

    pcapPacket->headerSize = packet_getHeaderSize(packet);
    pcapPacket->payloadLength = packet_getPayloadLength(packet);

    if(pcapPacket->payloadLength > 0) {
        pcapPacket->payload = g_new0(guchar, pcapPacket->payloadLength);
        packet_copyPayload(packet, 0, pcapPacket->payload, pcapPacket->payloadLength);
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
    debug("looking for socket associated with general key %s", key);
    Socket* socket = g_hash_table_lookup(interface->boundSockets, key);
    g_free(key);

    if(!socket) {
        /* now check the destination-specific key */
        in_addr_t peerIP = packet_getSourceIP(packet);
        in_port_t peerPort = packet_getSourcePort(packet);

        key = _networkinterface_getAssociationKey(interface, ptype, bindPort, peerIP, peerPort);
        debug("looking for socket associated with specific key %s", key);
        socket = g_hash_table_lookup(interface->boundSockets, key);
        g_free(key);
    }

    /* if the socket closed, just drop the packet */
    gint socketHandle = -1;
    if(socket) {
        socketHandle = *descriptor_getHandleReference((Descriptor*)socket);
        socket_pushInPacket(socket, packet);
    } else {
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
    }

    /* count our bandwidth usage by interface, and by socket handle if possible */
    tracker_addInputBytes(host_getTracker(worker_getActiveHost()), packet, socketHandle);
    if(interface->pcap) {
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
            if(length > interface->receiveBucket.bytesRemaining) {
                interface->receiveBucket.bytesRemaining = 0;
            } else {
                interface->receiveBucket.bytesRemaining -= length;
            }
        }
    }
}

static void _networkinterface_updatePacketHeader(Descriptor* descriptor, Packet* packet) {
    DescriptorType type = descriptor_getType(descriptor);
    if(type == DT_TCPSOCKET) {
        TCP* tcp = (TCP*)descriptor;
        tcp_networkInterfaceIsAboutToSendPacket(tcp, packet);
    }
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface, gint* socketHandle) {
    Packet* packet = NULL;

    while(!packet && !g_queue_is_empty(interface->rrQueue)) {
        /* do round robin to get the next packet from the next socket */
        Socket* socket = g_queue_pop_head(interface->rrQueue);
        packet = socket_pullOutPacket(socket);
        *socketHandle = *descriptor_getHandleReference((Descriptor*)socket);

        if(socket && packet) {
            _networkinterface_updatePacketHeader((Descriptor*)socket, packet);
        }

        if(socket_peekNextPacket(socket)) {
            /* socket has more packets, and is still reffed from before */
            g_queue_push_tail(interface->rrQueue, socket);
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            descriptor_unref((Descriptor*) socket);
        }
    }

    return packet;
}

/* first-in-first-out queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectFirstInFirstOut(NetworkInterface* interface, gint* socketHandle) {
    /* use packet priority field to select based on application ordering.
     * this is really a simplification of prioritizing on timestamps. */
    Packet* packet = NULL;

    while(!packet && !priorityqueue_isEmpty(interface->fifoQueue)) {
        /* do fifo to get the next packet from the next socket */
        Socket* socket = priorityqueue_pop(interface->fifoQueue);
        packet = socket_pullOutPacket(socket);
        *socketHandle = *descriptor_getHandleReference((Descriptor*)socket);

        if(socket && packet) {
            _networkinterface_updatePacketHeader((Descriptor*)socket, packet);
        }

        if(socket_peekNextPacket(socket)) {
            /* socket has more packets, and is still reffed from before */
            priorityqueue_push(interface->fifoQueue, socket);
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            descriptor_unref((Descriptor*) socket);
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
            case QDISC_MODE_RR: {
                packet = _networkinterface_selectRoundRobin(interface, &socketHandle);
                break;
            }
            case QDISC_MODE_FIFO:
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
            if(length >= interface->sendBucket.bytesRemaining) {
                interface->sendBucket.bytesRemaining = 0;
            } else {
                interface->sendBucket.bytesRemaining -= length;
            }
        }

        tracker_addOutputBytes(host_getTracker(worker_getActiveHost()), packet, socketHandle);
        if(interface->pcap) {
            _networkinterface_capturePacket(interface, packet);
        }

        /* sending side is done with its ref */
        packet_unref(packet);
    }
}

void networkinterface_wantsSend(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    /* track the new socket for sending if not already tracking */
    switch(interface->qdisc) {
        case QDISC_MODE_RR: {
            if(!g_queue_find(interface->rrQueue, socket)) {
                descriptor_ref(socket);
                g_queue_push_tail(interface->rrQueue, socket);
            }
            break;
        }
        case QDISC_MODE_FIFO:
        default: {
            if(!priorityqueue_find(interface->fifoQueue, socket)) {
                descriptor_ref(socket);
                priorityqueue_push(interface->fifoQueue, socket);
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
        gboolean logPcap, gchar* pcapDir, QDiscMode qdisc, guint64 interfaceReceiveLength) {
    NetworkInterface* interface = g_new0(NetworkInterface, 1);
    MAGIC_INIT(interface);

    interface->address = address;
    address_ref(interface->address);

    /* incoming packets get passed along to sockets */
    interface->boundSockets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, descriptor_unref);

    /* sockets tell us when they want to start sending */
    interface->rrQueue = g_queue_new();
    interface->fifoQueue = priorityqueue_new((GCompareDataFunc)_networkinterface_compareSocket, NULL, descriptor_unref);

    /* parse queuing discipline */
    interface->qdisc = (qdisc == QDISC_MODE_NONE) ? QDISC_MODE_FIFO : qdisc;

    if(logPcap) {
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
            interface->qdisc == QDISC_MODE_RR ? "rr" : "fifo");

    worker_countObject(OBJECT_TYPE_NETIFACE, COUNTER_TYPE_NEW);
    return interface;
}

void networkinterface_free(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* unref all sockets wanting to send */
    while(interface->rrQueue && !g_queue_is_empty(interface->rrQueue)) {
        Socket* socket = g_queue_pop_head(interface->rrQueue);
        descriptor_unref(socket);
    }
    g_queue_free(interface->rrQueue);

    priorityqueue_free(interface->fifoQueue);

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

    worker_countObject(OBJECT_TYPE_NETIFACE, COUNTER_TYPE_FREE);
}

