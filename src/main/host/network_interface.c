/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
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
#include "main/utility/priority_queue.h"
#include "main/utility/tagged_ptr.h"
#include "main/utility/utility.h"

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
    TokenBucket* tb_send;
    /* If we have scheduled a send task but it has not yet executed. */
    bool tb_send_refill_pending;

    /* the incoming token bucket implements traffic policing, i.e.,
     * packets that do not conform to incoming rate limits are dropped. */
    TokenBucket* tb_receive;
    /* If we have scheduled a receive task but it has not yet executed. */
    bool tb_receive_refill_pending;

    /* To support capturing incoming and outgoing packets */
    PcapWriter_BufWriter_File* pcap;

    MAGIC_DECLARE;
};

/* forward declarations */
static void _networkinterface_sendPackets(NetworkInterface* interface, Host* src);

static void _compatsocket_unrefTaggedVoid(void* taggedSocketPtr) {
    utility_assert(taggedSocketPtr != NULL);
    if (taggedSocketPtr == NULL) {
        return;
    }

    uintptr_t taggedSocket = (uintptr_t)taggedSocketPtr;
    CompatSocket socket = compatsocket_fromTagged(taggedSocket);
    compatsocket_unref(&socket);
}

static TokenBucket* _networkinterface_create_tb(uint64_t bwKiBps) {
    uint64_t refill_interval_nanos = SIMTIME_ONE_MILLISECOND;
    uint64_t refill_size = bwKiBps * 1024 / 1000;

    // The `CONFIG_MTU` part represents a "burst allowance" which is common in
    // token buckets. Only the `capacity` of the bucket is increased by
    // `CONFIG_MTU`, not the `refill_size`. Therefore, the long term rate limit
    // enforced by the token bucket (configured by `refill_size`) is not
    // affected much.
    //
    // What the burst allowance ensures is that we don't lose tokens that are
    // unused because we don't fragment packets. If we set the capacity of the
    // bucket to exactly the refill size (i.e., without the `CONFIG_MTU` burst
    // allowance) and there are only 1499 tokens left in this sending round, a
    // full packet would not fit. The next time the bucket refills, it adds
    // `refill_size` tokens but in doing so 1499 tokens would fall over the top
    // of the bucket; these tokens would represent wasted bandwidth, and could
    // potentially accumulate in every refill interval leading to a
    // significantly lower achievable bandwidth.
    //
    // A downside of the `CONFIG_MTU` burst allowance is that the sending rate
    // could possibly become "bursty" with a behavior such as:
    // - interval 1: send `refill_size` + `CONFIG_MTU` bytes, sending over the
    //   allowance by 1500 bytes
    // - refill: `refill_size` token gets added to the bucket
    // - interval 2: send `refill_size` - `CONFIG_MTU` bytes, sending under the
    //   allowance by 1500 bytes
    // - refill: `refill_size` token gets added to the bucket
    // - interval 3: send `refill_size` + `CONFIG_MTU` bytes, sending over the
    //   allowance by 1500 bytes
    // - repeat
    //
    // So it could become less smooth and more "bursty" even though the long
    // term average is maintained. But I don't think this would happen much in
    // practice, and we are batching sends for performance reasons.
    uint64_t capacity = refill_size + CONFIG_MTU;

    debug("creating token bucket with capacity=%" G_GUINT64_FORMAT " refill_size=%" G_GUINT64_FORMAT
          " refill_interval_nanos=%" G_GUINT64_FORMAT,
          capacity, refill_size, refill_interval_nanos);

    return tokenbucket_new(capacity, refill_size, refill_interval_nanos);
}

void networkinterface_startRefillingTokenBuckets(NetworkInterface* interface, Host* host,
                                                 uint64_t bwDownKiBps, uint64_t bwUpKiBps) {
    MAGIC_ASSERT(interface);
    // Set size and refill rates for token buckets.
    // This needs to be called when host is booting, i.e. when the worker exists.
    interface->tb_send = _networkinterface_create_tb(bwUpKiBps);
    interface->tb_receive = _networkinterface_create_tb(bwDownKiBps);
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
    utility_assert(interface->pcap != NULL);

    /* get the current time that the packet is being sent/received */
    SimulationTime now = worker_getCurrentSimulationTime();
    guint32 ts_sec = now / SIMTIME_ONE_SECOND;
    guint32 ts_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;

    int error = pcapwriter_writePacket(interface->pcap, ts_sec, ts_usec, packet);
    if (error) {
        /* if there was a non-recoverable error */
        warning("Fatal pcap logging error; stopping pcap logging for current interface");
        pcapwriter_free(interface->pcap);
        interface->pcap = NULL;
    }
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

static void _networkinterface_process_packet_in(Host* host, NetworkInterface* interface,
                                                Packet* packet) {
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

    /* record the packet before we process it, otherwise we may send more packets before we
       record this one and the order will be incorrect */
    if (interface->pcap) {
        _networkinterface_capturePacket(interface, packet);
    }

    /* if the socket closed, just drop the packet */
    if (socket.type != CST_NONE) {
        compatsocket_pushInPacket(&socket, host, packet);
    } else {
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
    }

    LegacySocket* legacySocket = NULL;
    switch (socket.type) {
        case CST_LEGACY_SOCKET:
            legacySocket = socket.object.as_legacy_socket;
            break;
        case CST_NONE:
            legacySocket = NULL;
            break;
    }

    /* count our bandwidth usage by interface, and by socket if possible */
    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL && legacySocket != NULL) {
        tracker_addInputBytes(tracker, packet, legacySocket);
    }
}

static void _networkinterface_local_packet_arrived_CB(Host* host, gpointer voidInterface,
                                                      gpointer voidPacket) {
    _networkinterface_process_packet_in(host, voidInterface, voidPacket);
}

static uint64_t _networkinterface_packet_tokens(const Packet* packet) {
    return (uint64_t)packet_getTotalSize(packet);
}

static void _networkinterface_continue_receiving_CB(Host* host, gpointer voidInterface,
                                                    gpointer userData) {
    NetworkInterface* interface = voidInterface;
    MAGIC_ASSERT(interface);
    interface->tb_receive_refill_pending = false;
    networkinterface_receivePackets(interface, host);
}

void networkinterface_receivePackets(NetworkInterface* interface, Host* host) {
    MAGIC_ASSERT(interface);

    /* we can only receive packets from the upstream router if we actually have one.
     * sending on loopback means we don't have a router, sending to a remote host
     * means we do have a router. */
    if(!interface->router) {
        return;
    }

    /* get the bootstrapping mode */
    gboolean is_bootstrapping = worker_isBootstrapActive();
    const Packet* peeked_packet = NULL;

    while ((peeked_packet = router_peek(interface->router))) {
        // Check if our rate limits allow us to receive the packet.
        if (!is_bootstrapping) {
            uint64_t required = _networkinterface_packet_tokens(peeked_packet);
            uint64_t remaining = 0, next_refill_nanos = 0;
            if (!tokenbucket_consume(
                    interface->tb_receive, required, &remaining, &next_refill_nanos)) {
                // We are rate limited for now, call back when we have more tokens.
                if (!interface->tb_receive_refill_pending) {
                    /* Call back when we'll have more receive tokens. */
                    TaskRef* recv_again =
                        taskref_new_bound(host_getID(host), _networkinterface_continue_receiving_CB,
                                          interface, NULL, NULL, NULL);
                    host_scheduleTaskWithDelay(host, recv_again, (SimulationTime)next_refill_nanos);
                    taskref_drop(recv_again);
                    interface->tb_receive_refill_pending = true;
                }
                return;
            }
        }

        /* we are now the owner of the packet reference from the router */
        Packet* packet = router_dequeue(interface->router);
        // We already peeked it, so it better be here when we pop it.
        utility_assert(packet);

        _networkinterface_process_packet_in(host, interface, packet);

        /* release reference from router */
        packet_unref(packet);
    }
}

static void _networkinterface_updatePacketHeader(Host* host, const CompatSocket* socket,
                                                 Packet* packet) {
    if (socket->type == CST_LEGACY_SOCKET) {
        LegacyFile* descriptor = (LegacyFile*)socket->object.as_legacy_socket;

        LegacyFileType type = legacyfile_getType(descriptor);
        if (type == DT_TCPSOCKET) {
            TCP* tcp = (TCP*)descriptor;
            tcp_networkInterfaceIsAboutToSendPacket(tcp, host, packet);
        }
    }
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface, Host* host,
                                                  LegacySocket** socketOut) {
    Packet* packet = NULL;

    while (!packet && !rrsocketqueue_isEmpty(&interface->rrQueue)) {
        /* do round robin to get the next packet from the next socket */
        CompatSocket socket = {0};
        bool found = rrsocketqueue_pop(&interface->rrQueue, &socket);

        if (!found) {
            continue;
        }

        packet = compatsocket_pullOutPacket(&socket, host);

        switch (socket.type) {
            case CST_LEGACY_SOCKET:
                *socketOut = socket.object.as_legacy_socket;
                break;
            case CST_NONE:
                *socketOut = NULL;
                break;
        }

        if (packet) {
            _networkinterface_updatePacketHeader(host, &socket, packet);
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
static Packet* _networkinterface_selectFirstInFirstOut(NetworkInterface* interface, Host* host,
                                                       LegacySocket** socketOut) {
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

        packet = compatsocket_pullOutPacket(&socket, host);

        switch (socket.type) {
            case CST_LEGACY_SOCKET:
                *socketOut = socket.object.as_legacy_socket;
                break;
            case CST_NONE:
                *socketOut = NULL;
                break;
        }

        if (packet) {
            _networkinterface_updatePacketHeader(host, &socket, packet);
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

static Packet* _networkinterface_pop_next_packet_out(NetworkInterface* interface, Host* host,
                                                     LegacySocket** socketOut) {
    MAGIC_ASSERT(interface);
    switch (interface->qdisc) {
        case Q_DISC_MODE_ROUND_ROBIN: {
            return _networkinterface_selectRoundRobin(interface, host, socketOut);
        }
        case Q_DISC_MODE_FIFO:
        default: {
            return _networkinterface_selectFirstInFirstOut(interface, host, socketOut);
        }
    }
}

static const Packet* _networkinterface_peek_next_packet_out(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    CompatSocket socket = {0};
    bool found = false;

    switch (interface->qdisc) {
        case Q_DISC_MODE_ROUND_ROBIN: {
            found = rrsocketqueue_peek(&interface->rrQueue, &socket);
            break;
        }
        case Q_DISC_MODE_FIFO:
        default: {
            found = fifosocketqueue_peek(&interface->fifoQueue, &socket);
            break;
        }
    }

    if (found) {
        return compatsocket_peekNextOutPacket(&socket);
    } else {
        return NULL;
    }
}

static void _networkinterface_continue_sending_CB(Host* host, gpointer voidInterface,
                                                  gpointer userData) {
    NetworkInterface* interface = voidInterface;
    MAGIC_ASSERT(interface);
    interface->tb_send_refill_pending = false;
    _networkinterface_sendPackets(interface, host);
}

static void _networkinterface_sendPackets(NetworkInterface* interface, Host* src) {
    MAGIC_ASSERT(interface);

    gboolean is_bootstrapping = worker_isBootstrapActive();
    const Packet* peeked_packet = NULL;

    while ((peeked_packet = _networkinterface_peek_next_packet_out(interface))) {
        // Local packets arrive on our own interface, they do not go through the
        // upstream router and do not consume bandwidth.
        bool is_local =
            address_toNetworkIP(interface->address) == packet_getDestinationIP(peeked_packet);

        // Check if our rate limits allows us to send the packet.
        if (!is_bootstrapping && !is_local) {
            uint64_t required = _networkinterface_packet_tokens(peeked_packet);
            uint64_t remaining = 0, next_refill_nanos = 0;
            if (!tokenbucket_consume(
                    interface->tb_send, required, &remaining, &next_refill_nanos)) {
                // We are rate limited for now, call back when we have more tokens.
                if (!interface->tb_send_refill_pending) {
                    /* Call back when we'll have more send tokens. */
                    TaskRef* send_again =
                        taskref_new_bound(host_getID(src), _networkinterface_continue_sending_CB,
                                          interface, NULL, NULL, NULL);
                    host_scheduleTaskWithDelay(src, send_again, (SimulationTime)next_refill_nanos);
                    taskref_drop(send_again);
                    interface->tb_send_refill_pending = true;
                }
                return;
            }
        }

        // Now actually pop and send the packet.
        LegacySocket* legacySocket = NULL;
        Packet* packet = _networkinterface_pop_next_packet_out(interface, src, &legacySocket);
        // We already peeked it, so it better be here when we pop it.
        utility_assert(packet);

        packet_addDeliveryStatus(packet, PDS_SND_INTERFACE_SENT);

        /* record the packet early before we do anything else */
        if(interface->pcap) {
            _networkinterface_capturePacket(interface, packet);
        }

        /* now actually send the packet somewhere */
        if (is_local) {
            // Arrives directly back on our interface.
            packet_ref(packet);
            TaskRef* packetTask =
                taskref_new_bound(host_getID(src), _networkinterface_local_packet_arrived_CB,
                                  interface, packet, NULL, packet_unrefTaskFreeFunc);
            host_scheduleTaskWithDelay(src, packetTask, 1);
            taskref_drop(packetTask);
        } else {
            /* let the upstream router send to remote with appropriate delays.
             * if we get here we are not loopback and should have been assigned a router. */
            utility_assert(interface->router);
            router_forward(interface->router, src, packet);
        }

        Tracker* tracker = host_getTracker(src);
        if (tracker != NULL && legacySocket != NULL) {
            tracker_addOutputBytes(tracker, packet, legacySocket);
        }

        /* sending side is done with its ref */
        packet_unref(packet);
    }
}

void networkinterface_wantsSend(NetworkInterface* interface, Host* host,
                                const CompatSocket* socket) {
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
    _networkinterface_sendPackets(interface, host);
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

NetworkInterface* networkinterface_new(Host* host, Address* address, gchar* pcapDir,
                                       guint32 pcapCaptureSize, QDiscMode qdisc,
                                       guint64 interfaceReceiveLength) {
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

    if (pcapDir != NULL) {
        GString* filename = g_string_new(NULL);
        g_string_append(filename, pcapDir);

        /* Append trailing slash if not present */
        if (!g_str_has_suffix(filename->str, "/")) {
            g_string_append(filename, "/");
        }

        g_string_append_printf(filename, "%s-%s.pcap", address_toHostName(interface->address),
                               address_toHostIPString(interface->address));

        interface->pcap = pcapwriter_new(filename->str, pcapCaptureSize);
        g_string_free(filename, TRUE);
    }

    debug("bringing up network interface '%s' at '%s' using queuing discipline %s",
          address_toHostName(interface->address), address_toHostIPString(interface->address),
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

