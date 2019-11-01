/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _NetworkInterfaceFlags NetworkInterfaceFlags;
enum _NetworkInterfaceFlags {
    NIF_NONE = 0,
    NIF_SENDING = 1 << 0,
    NIF_RECEIVING = 1 << 1,
};

struct _NetworkInterface {
    NetworkInterfaceFlags flags;
    QDiscMode qdisc;

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
        gboolean logPcap, gchar* pcapDir, QDiscMode qdisc, guint64 interfaceReceiveLength) {
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

    info("bringing up network interface '%s' at '%s', %"G_GUINT64_FORMAT" KiB/s up and %"G_GUINT64_FORMAT" KiB/s down using queuing discipline %s",
            address_toHostName(interface->address), address_toHostIPString(interface->address), bwUpKiBps, bwDownKiBps,
            interface->qdisc == QDISC_MODE_RR ? "rr" : "fifo");

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

guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->bwUpKiBps;
}

guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface) {
    MAGIC_ASSERT(interface);
    return interface->bwDownKiBps;
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

static void _networkinterface_runReceievedTask(NetworkInterface* interface, gpointer userData) {
    networkinterface_received(interface);
}

static void _networkinterface_scheduleNextReceive(NetworkInterface* interface) {
    /* the next packets need to be received and processed */
    SimulationTime batchTime = options_getInterfaceBatchTime(worker_getOptions());

    gboolean bootstrapping = worker_isBootstrapActive();

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
        if(!bootstrapping) {
            interface->receiveNanosecondsConsumed += (length * interface->timePerByteDown);
        }

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
        Task* receivedTask = task_new((TaskCallbackFunc)_networkinterface_runReceievedTask,
                interface, NULL, NULL, NULL);
        worker_scheduleTask(receivedTask, receiveTime);
        task_unref(receivedTask);
    }
}

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet) {
    MAGIC_ASSERT(interface);

    /* a packet arrived. lets try to receive or buffer it.
     * we don't drop control-only packets, so don't include header size in length */
    guint length = packet_getPayloadLength(packet);// + packet_getHeaderSize(packet);
    gssize space = interface->inBufferSize - interface->inBufferLength;
    utility_assert(space >= 0);

    /* we don't drop control packets */
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

static void _networkinterface_runSentTask(NetworkInterface* interface, gpointer userData) {
    networkinterface_sent(interface);
}


static void _networkinterface_scheduleNextSend(NetworkInterface* interface) {
    /* the next packet needs to be sent according to bandwidth limitations.
     * we need to spend time sending it before sending the next. */
    SimulationTime batchTime = options_getInterfaceBatchTime(worker_getOptions());

    gboolean bootstrapping = worker_isBootstrapActive();

    /* loop until we find a socket that has something to send */
    while(interface->sendNanosecondsConsumed <= batchTime) {
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
            /* packet will arrive on our own interface */
            packet_ref(packet);
            Task* packetTask = task_new((TaskCallbackFunc)networkinterface_packetArrived,
                    interface, packet, NULL, (TaskArgumentFreeFunc)packet_unref);
            worker_scheduleTask(packetTask, 1);
            task_unref(packetTask);
        } else {
            /* let the worker send to remote with appropriate delays */
            worker_sendPacket(packet);
        }

        /* successfully sent, calculate how long it took to 'send' this packet */
        if(!bootstrapping) {
            guint length = packet_getPayloadLength(packet) + packet_getHeaderSize(packet);
            interface->sendNanosecondsConsumed += (length * interface->timePerByteUp);
        }

        tracker_addOutputBytes(host_getTracker(worker_getActiveHost()), packet, socketHandle);
        if(interface->pcap) {
            _networkinterface_capturePacket(interface, packet);
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
        Task* sentTask = task_new((TaskCallbackFunc)_networkinterface_runSentTask,
                interface, NULL, NULL, NULL);
        worker_scheduleTask(sentTask, sendTime);
        task_unref(sentTask);
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
