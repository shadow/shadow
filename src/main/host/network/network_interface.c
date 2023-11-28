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
#include "main/host/network/network_interface.h"
#include "main/host/network/network_queuing_disciplines.h"
#include "main/host/protocol.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/utility/priority_queue.h"
#include "main/utility/tagged_ptr.h"
#include "main/utility/utility.h"

struct _NetworkInterface {
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

    /* To support capturing incoming and outgoing packets */
    PcapWriter_BufWriter_File* pcap;

    MAGIC_DECLARE;
};

static void _compatsocket_unrefTaggedVoid(void* taggedSocketPtr) {
    utility_debugAssert(taggedSocketPtr != NULL);
    if (taggedSocketPtr == NULL) {
        return;
    }

    uintptr_t taggedSocket = (uintptr_t)taggedSocketPtr;
    CompatSocket socket = compatsocket_fromTagged(taggedSocket);
    compatsocket_unref(&socket);
}

/* The address and ports must be in network byte order. */
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

/* The address and ports must be in network byte order. */
gboolean networkinterface_isAssociated(NetworkInterface* interface, ProtocolType type,
                                       in_port_t port, in_addr_t peerAddr, in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    gboolean isFound = FALSE;

    gchar* key = _networkinterface_getAssociationKey(interface, type, port, peerAddr, peerPort);
    isFound = g_hash_table_contains(interface->boundSockets, key);
    g_free(key);

    return isFound;
}

void networkinterface_associate(NetworkInterface* interface, const CompatSocket* socket,
                                ProtocolType type, in_port_t port, in_addr_t peerIP,
                                in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_getAssociationKey(interface, type, port, peerIP, peerPort);

    /* make sure there is no collision */
    utility_debugAssert(!g_hash_table_contains(interface->boundSockets, key));

    /* need to store our own reference to the socket object */
    CompatSocket newSocketRef = compatsocket_refAs(socket);

    /* insert to our storage, key is now owned by table */
    bool key_did_not_exist = g_hash_table_replace(
        interface->boundSockets, key, (void*)compatsocket_toTagged(&newSocketRef));

    utility_debugAssert(key_did_not_exist);

    trace("associated socket key %s", key);
}

void networkinterface_disassociate(NetworkInterface* interface, ProtocolType type, in_port_t port,
                                   in_addr_t peerIP, in_port_t peerPort) {
    MAGIC_ASSERT(interface);

    gchar* key = _networkinterface_getAssociationKey(interface, type, port, peerIP, peerPort);

    /* we will no longer receive packets for this port, this unrefs descriptor */
    /* TODO: Return an error if the disassociation fails. Generally the
     * calling code should only try to disassociate a socket if it thinks that the
     * socket is actually associated with this interface, and if it's not, then
     * it's probably an error. But TCP sockets will disassociate all sockets
     * (including ones that have never been associated) and will try to
     * disassociate the same socket multiple times, so we can't just add an assert
     * here. */
    g_hash_table_remove(interface->boundSockets, key);

    trace("disassociated socket key %s", key);
    g_free(key);
}

static void _networkinterface_capturePacket(NetworkInterface* interface, Packet* packet) {
    utility_debugAssert(interface->pcap != NULL);

    /* get the current time that the packet is being sent/received */
    CSimulationTime now = worker_getCurrentSimulationTime();
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

void networkinterface_push(NetworkInterface* interface, Packet* packet, CEmulatedTime recvTime) {
    MAGIC_ASSERT(interface);

    const Host* host = worker_getCurrentHost();

    /* get the next packet */
    utility_debugAssert(packet);

    /* successfully received */
    packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_RECEIVED);

    /* hand it off to the correct socket layer */
    ProtocolType ptype = packet_getProtocol(packet);
    in_port_t bindPort = packet_getDestinationPort(packet);
    in_addr_t peerIP = packet_getSourceIP(packet);
    in_port_t peerPort = packet_getSourcePort(packet);

    /* first check for a socket with the specific association */
    gchar* key = _networkinterface_getAssociationKey(interface, ptype, bindPort, peerIP, peerPort);
    trace("looking for socket associated with specific key %s", key);

    CompatSocket socket = _boundsockets_lookup(interface->boundSockets, key);
    g_free(key);

    if (socket.type == CST_NONE) {
        /* then check for a socket with a wildcard association */
        key = _networkinterface_getAssociationKey(interface, ptype, bindPort, 0, 0);
        trace("looking for socket associated with general key %s", key);
        socket = _boundsockets_lookup(interface->boundSockets, key);
        g_free(key);
    }

    /* record the packet before we process it, otherwise we may send more packets before we
       record this one and the order will be incorrect */
    if (interface->pcap) {
        _networkinterface_capturePacket(interface, packet);
    }

    /* pushing a packet to the socket may cause the socket to be disassociated and freed and cause
     * our socket pointer to become dangling while we're using it, so we need to increase its ref
     * count */
    if (socket.type != CST_NONE) {
        socket = compatsocket_refAs(&socket);
    }

    /* if the socket closed, just drop the packet */
    if (socket.type != CST_NONE) {
        compatsocket_pushInPacket(&socket, host, packet, recvTime);
    } else {
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
    }

    /* count our bandwidth usage by interface, and by socket if possible */
    Tracker* tracker = host_getTracker(host);
    if (tracker != NULL && socket.type != CST_NONE) {
        tracker_addInputBytes(tracker, packet, &socket);
    }

    if (socket.type != CST_NONE) {
        compatsocket_unref(&socket);
    }
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface, const Host* host,
                                                  CompatSocket* socketOut) {
    Packet* packet = NULL;

    while (!packet && !rrsocketqueue_isEmpty(&interface->rrQueue)) {
        /* do round robin to get the next packet from the next socket */
        CompatSocket socket = {0};
        bool found = rrsocketqueue_pop(&interface->rrQueue, &socket);

        if (!found) {
            continue;
        }

        packet = compatsocket_pullOutPacket(&socket, host);

        if (packet == NULL) {
            /* socket had no packet, unref it from the sendable queue */
            compatsocket_unref(&socket);
            continue;
        }

        /* we're returning the socket, so we must ref it */
        *socketOut = compatsocket_refAs(&socket);

        if (compatsocket_hasDataToSend(&socket)) {
            /* socket has more packets, and is still reffed from before */
            if (!rrsocketqueue_find(&interface->rrQueue, &socket)) {
                rrsocketqueue_push(&interface->rrQueue, &socket);
            } else {
                /* socket is already in the rr queue (probably added by `compatsocket_pullOutPacket`
                 * above); unref it from the sendable queue */
                compatsocket_unref(&socket);
            }
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            compatsocket_unref(&socket);
        }

        return packet;
    }

    return NULL;
}

/* first-in-first-out queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectFirstInFirstOut(NetworkInterface* interface,
                                                       const Host* host, CompatSocket* socketOut) {
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

        if (packet == NULL) {
            /* socket had no packet, unref it from the sendable queue */
            compatsocket_unref(&socket);
            continue;
        }

        /* we're returning the socket, so we must ref it */
        *socketOut = compatsocket_refAs(&socket);

        if (compatsocket_hasDataToSend(&socket)) {
            /* socket has more packets, and is still reffed from before */
            if (!fifosocketqueue_find(&interface->fifoQueue, &socket)) {
                fifosocketqueue_push(&interface->fifoQueue, &socket);
            } else {
                /* socket is already in the fifo (probably added by `compatsocket_pullOutPacket`
                 * above); unref it from the sendable queue */
                compatsocket_unref(&socket);
            }
        } else {
            /* socket has no more packets, unref it from the sendable queue */
            compatsocket_unref(&socket);
        }

        return packet;
    }

    return NULL;
}

static Packet* _networkinterface_pop_next_packet_out(NetworkInterface* interface, const Host* host,
                                                     CompatSocket* socketOut) {
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

Packet* networkinterface_pop(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    const Host* src = worker_getCurrentHost();

    // We will have an owned reference, so need to deref later.
    CompatSocket socket = {0};

    // Now actually pop and send the packet.
    Packet* packet = _networkinterface_pop_next_packet_out(interface, src, &socket);

    if (packet != NULL) {
        packet_addDeliveryStatus(packet, PDS_SND_INTERFACE_SENT);

        /* record the packet early before we do anything else */
        if(interface->pcap) {
            _networkinterface_capturePacket(interface, packet);
        }

        Tracker* tracker = host_getTracker(src);
        if (tracker != NULL && socket.type != CST_NONE) {
            tracker_addOutputBytes(tracker, packet, &socket);
        }
    }

    if (socket.type != CST_NONE) {
        compatsocket_unref(&socket);
    }

    return packet;
}

// Add the socket to the list of sockets that have data ready for us to send
// out to the network.
void networkinterface_wantsSend(NetworkInterface* interface, const CompatSocket* socket) {
    MAGIC_ASSERT(interface);

    if (!compatsocket_hasDataToSend(socket)) {
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
}

void networkinterface_removeAllSockets(NetworkInterface* interface) {
    /* we want to unref all sockets, but also want to keep the network interface in a valid state */

    rrsocketqueue_destroy(&interface->rrQueue, compatsocket_unref);
    fifosocketqueue_destroy(&interface->fifoQueue, compatsocket_unref);

    rrsocketqueue_init(&interface->rrQueue);
    fifosocketqueue_init(&interface->fifoQueue);

    g_hash_table_remove_all(interface->boundSockets);
}

NetworkInterface* networkinterface_new(Address* address, const char* name, const gchar* pcapDir,
                                       guint32 pcapCaptureSize, QDiscMode qdisc) {
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

        g_string_append_printf(filename, "%s.pcap", name);

        interface->pcap = pcapwriter_new(filename->str, pcapCaptureSize);
        g_string_free(filename, TRUE);
    }

    debug("bringing up network interface '%s' for host '%s' at '%s' using queuing discipline %s",
          name, address_toHostName(interface->address), address_toHostIPString(interface->address),
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

    address_unref(interface->address);

    if(interface->pcap) {
        pcapwriter_free(interface->pcap);
    }

    MAGIC_CLEAR(interface);
    g_free(interface);

    worker_count_deallocation(NetworkInterface);
}
