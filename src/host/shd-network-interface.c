/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

enum NetworkInterfaceFlags {
    NIF_NONE = 0,
    NIF_SENDING = 1 << 0,
    NIF_RECEIVING = 1 << 1,
};

enum NetworkInterfaceQDisc {
    NIQ_NONE=0, NIQ_FIFO=1, NIQ_RR=2,
};

struct _NetworkInterface {
    enum NetworkInterfaceFlags flags;
    enum NetworkInterfaceQDisc qdisc;

    Address* address;

    guint64 bwDownKiBps;
    gdouble timePerByteDown;
    guint64 bwUpKiBps;
    gdouble timePerByteUp;

    /* (protocol,port)-to-socket bindings */
    GHashTable* boundSockets;

    /* NIC input queue */
    GQueue* inBuffer;
    gsize inBufferSize;
    gsize inBufferLength;

    /* Transports wanting to send data out */
    GQueue* rrQueue;
    PriorityQueue* fifoQueue;

    /* bandwidth accounting */
    SimulationTime lastTimeReceived;
    SimulationTime lastTimeSent;
    gdouble sendNanosecondsConsumed;
    gdouble receiveNanosecondsConsumed;

    PCapWriter* pcap;

    MAGIC_DECLARE;
};

static gint _networkinterface_compareSocket(const Socket* sa, const Socket* sb, gpointer userData) {
    Packet* pa = socket_peekNextPacket(sa);
    Packet* pb = socket_peekNextPacket(sb);
    return packet_getPriority(pa) > packet_getPriority(pb) ? +1 : -1;
}

NetworkInterface* networkinterface_new(Address* address, guint64 bwDownKiBps, guint64 bwUpKiBps,
        gboolean logPcap, gchar* pcapDir, gchar* qdisc, guint64 interfaceReceiveLength) {
    NetworkInterface* interface = g_new0(NetworkInterface, 1);
    MAGIC_INIT(interface);

    interface->address = address;
    address_ref(interface->address);

    /* interface speeds */
    interface->bwUpKiBps = bwUpKiBps;
    gdouble bytesPerSecond = (gdouble)(bwUpKiBps * 1024);
    interface->timePerByteUp = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);
    interface->bwDownKiBps = bwDownKiBps;
    bytesPerSecond = (gdouble)(bwDownKiBps * 1024);
    interface->timePerByteDown = (gdouble) (((gdouble)SIMTIME_ONE_SECOND) / bytesPerSecond);

    /* incoming packet buffer */
    interface->inBuffer = g_queue_new();
    interface->inBufferSize = interfaceReceiveLength;

    /* incoming packets get passed along to sockets */
    interface->boundSockets = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, descriptor_unref);

    /* sockets tell us when they want to start sending */
    interface->rrQueue = g_queue_new();
    interface->fifoQueue = priorityqueue_new((GCompareDataFunc)_networkinterface_compareSocket, NULL, descriptor_unref);

    /* parse queuing discipline */
    if (qdisc && !g_ascii_strcasecmp(qdisc, "rr")) {
        interface->qdisc = NIQ_RR;
    } else {
        interface->qdisc = NIQ_FIFO;
    }

    if(logPcap) {
        GString* filename = g_string_new(NULL);
        g_string_printf(filename, "%s-%s",
                address_toHostName(interface->address),
                address_toHostIPString(interface->address));
        interface->pcap = pcapwriter_new(pcapDir, filename->str);
        g_string_free(filename, TRUE);
    }

    info("bringing up network interface '%s' at '%s', %"G_GUINT64_FORMAT" KiB/s up and %"G_GUINT64_FORMAT" KiB/s down using queuing discipline %s",
            address_toHostName(interface->address), address_toHostIPString(interface->address), bwUpKiBps, bwDownKiBps,
            interface->qdisc == NIQ_RR ? "rr" : "fifo");

    return interface;
}

void networkinterface_free(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* unref all packets sitting in our input buffer */
    while(interface->inBuffer && !g_queue_is_empty(interface->inBuffer)) {
        Packet* packet = g_queue_pop_head(interface->inBuffer);
        packet_unref(packet);
    }
    g_queue_free(interface->inBuffer);

    /* unref all sockets wanting to send */
    while(interface->rrQueue && !g_queue_is_empty(interface->rrQueue)) {
        Socket* socket = g_queue_pop_head(interface->rrQueue);
        descriptor_unref(socket);
    }
    g_queue_free(interface->rrQueue);

    priorityqueue_free(interface->fifoQueue);

    g_hash_table_destroy(interface->boundSockets);

    dns_deregister(worker_getDNS(), interface->address);
    address_unref(interface->address);

    if(interface->pcap) {
        pcapwriter_free(interface->pcap);
    }

    MAGIC_CLEAR(interface);
    g_free(interface);
}

Address* networkinterface_getAddress(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->address;
}

// TODO remove this in favor of address func above
in_addr_t networkinterface_getIPAddress(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return address_toNetworkIP(interface->address);
}

// TODO remove this in favor of address func above
gchar* networkinterface_getIPName(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return address_toHostIPString(interface->address);
}

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->bwUpKiBps;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->bwDownKiBps;
}

gboolean networkinterface_isAssociated(NetworkInterface* interface, gint key) {
    MAGIC_ASSERT(interface);

    if(g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key))) {
        return TRUE;
    } else {
        return FALSE;
    }
}

guint networkinterface_getAssociationCount(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return g_hash_table_size(interface->boundSockets);
}

void networkinterface_associate(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    gint key = socket_getAssociationKey(socket);

    /* make sure there is no collision */
    utility_assert(!networkinterface_isAssociated(interface, key));

    /* insert to our storage */
    g_hash_table_replace(interface->boundSockets, GINT_TO_POINTER(key), socket);
    descriptor_ref(socket);
}

void networkinterface_disassociate(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    gint key = socket_getAssociationKey(socket);

    /* we will no longer receive packets for this port, this unrefs descriptor */
    g_hash_table_remove(interface->boundSockets, GINT_TO_POINTER(key));
}

static void _networkinterface_scheduleNextReceive(NetworkInterface* interface) {
    /* the next packets need to be received and processed */
    SimulationTime batchTime = worker_getConfig()->interfaceBatchTime;

    /* receive packets in batches */
    while(!g_queue_is_empty(interface->inBuffer) &&
            interface->receiveNanosecondsConsumed <= batchTime) {
        /* get the next packet */
        Packet* packet = g_queue_pop_head(interface->inBuffer);
        utility_assert(packet);

        /* successfully received */
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_RECEIVED);

        /* free up buffer space */
        guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
        interface->inBufferLength -= length;

        /* calculate how long it took to 'receive' this packet */
        interface->receiveNanosecondsConsumed += (length * interface->timePerByteDown);

        /* hand it off to the correct socket layer */
        gint key = packet_getDestinationAssociationKey(packet);
        Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

        /* if the socket closed, just drop the packet */
        gint socketHandle = -1;
        if(socket) {
            socketHandle = *descriptor_getHandleReference((Descriptor*)socket);
            socket_pushInPacket(socket, packet);
        } else {
            packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
        }

        /* count our bandwidth usage by interface, and by socket handle if possible */
        tracker_addInputBytes(host_getTracker(worker_getCurrentHost()), packet, socketHandle);
        if(interface->pcap) {
            pcapwriter_writePacket(interface->pcap, packet);
        }

        packet_unref(packet);
    }

    /*
     * we need to call back and try to receive more, even if we didnt consume all
     * of our batch time, because we might have more packets to receive then.
     */
    SimulationTime receiveTime = (SimulationTime) floor(interface->receiveNanosecondsConsumed);
    if(receiveTime >= SIMTIME_ONE_NANOSECOND) {
        /* we are 'receiving' the packets */
        interface->flags |= NIF_RECEIVING;
        /* call back when the packets are 'received' */
        InterfaceReceivedEvent* event = interfacereceived_new(interface);
        /* event destination is our node */
        worker_scheduleEvent((Event*)event, receiveTime, 0);
    }
}

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet) {
    MAGIC_ASSERT(interface);

    /* a packet arrived. lets try to receive or buffer it */
    guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
    gssize space = interface->inBufferSize - interface->inBufferLength;
    utility_assert(space >= 0);

    if(length <= space) {
        /* we have space to buffer it */
        packet_ref(packet);
        g_queue_push_tail(interface->inBuffer, packet);
        interface->inBufferLength += length;
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_BUFFERED);

        /* we need a trigger if we are not currently receiving */
        if(!(interface->flags & NIF_RECEIVING)) {
            _networkinterface_scheduleNextReceive(interface);
        }
    } else {
        /* buffers are full, drop packet */
        packet_addDeliveryStatus(packet, PDS_RCV_INTERFACE_DROPPED);
    }
}

void networkinterface_packetDropped(NetworkInterface* interface, Packet* packet) {
    MAGIC_ASSERT(interface);

    /* hand it off to the correct socket layer */
    gint key = packet_getSourceAssociationKey(packet);
    Socket* socket = g_hash_table_lookup(interface->boundSockets, GINT_TO_POINTER(key));

    /* if the socket closed, just drop the packet */
    gint socketHandle = -1;
    if(socket) {
        socketHandle = *descriptor_getHandleReference((Descriptor*)socket);
        socket_dropPacket(socket, packet);
    }
}

void networkinterface_received(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* we just finished receiving some packets */
    interface->flags &= ~NIF_RECEIVING;

    /* decide how much delay we get to absorb based on the passed time */
    SimulationTime now = worker_getCurrentTime();
    SimulationTime absorbInterval = now - interface->lastTimeReceived;

    if(absorbInterval > 0) {
        /* decide how much delay we get to absorb based on the passed time */
        gdouble newConsumed = interface->receiveNanosecondsConsumed - absorbInterval;
        interface->receiveNanosecondsConsumed = MAX(0, newConsumed);
    }

    interface->lastTimeReceived = now;

    /* now try to receive the next ones */
    _networkinterface_scheduleNextReceive(interface);
}

/* round robin queuing discipline ($ man tc)*/
static Packet* _networkinterface_selectRoundRobin(NetworkInterface* interface, gint* socketHandle) {
    Packet* packet = NULL;

    while(!packet && !g_queue_is_empty(interface->rrQueue)) {
        /* do round robin to get the next packet from the next socket */
        Socket* socket = g_queue_pop_head(interface->rrQueue);
        packet = socket_pullOutPacket(socket);
        *socketHandle = *descriptor_getHandleReference((Descriptor*)socket);

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

static void _networkinterface_scheduleNextSend(NetworkInterface* interface) {
    /* the next packet needs to be sent according to bandwidth limitations.
     * we need to spend time sending it before sending the next. */
    SimulationTime batchTime = worker_getConfig()->interfaceBatchTime;

    /* loop until we find a socket that has something to send */
    while(interface->sendNanosecondsConsumed <= batchTime) {
        gint socketHandle = -1;

        /* choose which packet to send next based on our queuing discipline */
        Packet* packet;
        switch(interface->qdisc) {
            case NIQ_RR: {
                packet = _networkinterface_selectRoundRobin(interface, &socketHandle);
                break;
            }
            case NIQ_FIFO:
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
        if(networkinterface_getIPAddress(interface) == packet_getDestinationIP(packet)) {
            /* packet will arrive on our own interface */
            PacketArrivedEvent* event = packetarrived_new(packet);
            /* event destination is our node */
            worker_scheduleEvent((Event*)event, 1, 0);
        } else {
            /* let the worker schedule with appropriate delays */
            worker_schedulePacket(packet);
        }

        /* successfully sent, calculate how long it took to 'send' this packet */
        guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
        interface->sendNanosecondsConsumed += (length * interface->timePerByteUp);

        tracker_addOutputBytes(host_getTracker(worker_getCurrentHost()), packet, socketHandle);
        if(interface->pcap) {
            pcapwriter_writePacket(interface->pcap, packet);
        }

        /* sending side is done with its ref */
        packet_unref(packet);
    }

    /*
     * we need to call back and try to send more, even if we didnt consume all
     * of our batch time, because we might have more packets to send then.
     */
    SimulationTime sendTime = (SimulationTime) floor(interface->sendNanosecondsConsumed);
    if(sendTime >= SIMTIME_ONE_NANOSECOND) {
        /* we are 'sending' the packets */
        interface->flags |= NIF_SENDING;
        /* call back when the packets are 'sent' */
        InterfaceSentEvent* event = interfacesent_new(interface);
        /* event destination is our node */
        worker_scheduleEvent((Event*)event, sendTime, 0);
    }
}

void networkinterface_wantsSend(NetworkInterface* interface, Socket* socket) {
    MAGIC_ASSERT(interface);

    /* track the new socket for sending if not already tracking */
    switch(interface->qdisc) {
        case NIQ_RR: {
            if(!g_queue_find(interface->rrQueue, socket)) {
                descriptor_ref(socket);
                g_queue_push_tail(interface->rrQueue, socket);
            }
            break;
        }
        case NIQ_FIFO:
        default: {
            if(!priorityqueue_find(interface->fifoQueue, socket)) {
                descriptor_ref(socket);
                priorityqueue_push(interface->fifoQueue, socket);
            }
            break;
        }
    }

    /* trigger a send if we are currently idle */
    if(!(interface->flags & NIF_SENDING)) {
        _networkinterface_scheduleNextSend(interface);
    }
}

void networkinterface_sent(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);

    /* we just finished sending some packets */
    interface->flags &= ~NIF_SENDING;

    /* decide how much delay we get to absorb based on the passed time */
    SimulationTime now = worker_getCurrentTime();
    SimulationTime absorbInterval = now - interface->lastTimeSent;

    if(absorbInterval > 0) {
        gdouble newConsumed = interface->sendNanosecondsConsumed - absorbInterval;
        interface->sendNanosecondsConsumed = MAX(0, newConsumed);
    }

    interface->lastTimeSent = now;

    /* now try to send the next ones */
    _networkinterface_scheduleNextSend(interface);
}
