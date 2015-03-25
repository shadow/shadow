/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-safe structure representing a data/network packet */

typedef struct _PacketLocalHeader PacketLocalHeader;
struct _PacketLocalHeader {
    enum ProtocolLocalFlags flags;
    gint sourceDescriptorHandle;
    gint destinationDescriptorHandle;
    in_port_t port;
};

typedef struct _PacketUDPHeader PacketUDPHeader;
struct _PacketUDPHeader {
    enum ProtocolUDPFlags flags;
    in_addr_t sourceIP;
    in_port_t sourcePort;
    in_addr_t destinationIP;
    in_port_t destinationPort;
};

struct _Packet {
    GMutex lock;
    guint referenceCount;

    enum ProtocolType protocol;
    gpointer header;
    gpointer payload;
    guint payloadLength;

    /* tracks application priority so we flush packets from the interface to
     * the wire in the order intended by the application. this is used in
     * the default FIFO network interface scheduling discipline.
     * smaller values have greater priority.
     */
    gdouble priority;

    PacketDeliveryStatusFlags allStatus;
    GQueue* orderedStatus;

    SimulationTime dropNotificationDelay;

    MAGIC_DECLARE;
};

Packet* packet_new(gconstpointer payload, gsize payloadLength) {
    Packet* packet = g_new0(Packet, 1);
    MAGIC_INIT(packet);

    g_mutex_init(&(packet->lock));
    packet->referenceCount = 1;

    if(payload != NULL && payloadLength > 0) {
        packet->payload = g_malloc0(payloadLength);
        g_memmove(packet->payload, payload, payloadLength);
        packet->payloadLength = payloadLength;
        utility_assert(packet->payload);

        /* application data needs a priority ordering for FIFO onto the wire */
        packet->priority = host_getNextPacketPriority(worker_getCurrentHost());
    }

    packet->orderedStatus = g_queue_new();

    return packet;
}

static void _packet_free(Packet* packet) {
    MAGIC_ASSERT(packet);

    g_mutex_clear(&(packet->lock));

    if(packet->protocol == PTCP) {
        PacketTCPHeader* header = (PacketTCPHeader*)packet->header;
        if(header->selectiveACKs) {
            g_list_free(header->selectiveACKs);
        }
    }

    if(packet->header) {
        g_free(packet->header);
    }
    if(packet->payload) {
        g_free(packet->payload);
    }
    if(packet->orderedStatus) {
        g_queue_free(packet->orderedStatus);
    }

    MAGIC_CLEAR(packet);
    g_free(packet);
}

static void _packet_lock(Packet* packet) {
    MAGIC_ASSERT(packet);
    g_mutex_lock(&(packet->lock));
}

static void _packet_unlock(Packet* packet) {
    MAGIC_ASSERT(packet);
    g_mutex_unlock(&(packet->lock));
}

void packet_ref(Packet* packet) {
    _packet_lock(packet);
    (packet->referenceCount)++;
    _packet_unlock(packet);
}

void packet_unref(Packet* packet) {
    _packet_lock(packet);

    (packet->referenceCount)--;
    if(packet->referenceCount == 0) {
        _packet_unlock(packet);
        packet_addDeliveryStatus(packet, PDS_DESTROYED);
        _packet_free(packet);
    } else {
        _packet_unlock(packet);
    }
}

gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data) {
    /* packet1 for one worker might be packet2 for another, dont lock both
     * at once or a deadlock will occur */
    guint sequence1 = 0, sequence2 = 0;

    _packet_lock(packet1);
    utility_assert(packet1->protocol == PTCP);
    sequence1 = ((PacketTCPHeader*)(packet1->header))->sequence;
    _packet_unlock(packet1);

    _packet_lock(packet2);
    utility_assert(packet2->protocol == PTCP);
    sequence2 = ((PacketTCPHeader*)(packet2->header))->sequence;
    _packet_unlock(packet2);

    return sequence1 < sequence2 ? -1 : sequence1 > sequence2 ? 1 : 0;
}

void packet_setLocal(Packet* packet, enum ProtocolLocalFlags flags,
        gint sourceDescriptorHandle, gint destinationDescriptorHandle, in_port_t port) {
    _packet_lock(packet);
    utility_assert(!(packet->header) && packet->protocol == PNONE);
    utility_assert(port > 0);

    PacketLocalHeader* header = g_new0(PacketLocalHeader, 1);

    header->flags = flags;
    header->sourceDescriptorHandle = sourceDescriptorHandle;
    header->destinationDescriptorHandle = destinationDescriptorHandle;
    header->port = port;

    packet->header = header;
    packet->protocol = PLOCAL;
    _packet_unlock(packet);
}

void packet_setUDP(Packet* packet, enum ProtocolUDPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort) {
    _packet_lock(packet);
    utility_assert(!(packet->header) && packet->protocol == PNONE);
    utility_assert(sourceIP && sourcePort && destinationIP && destinationPort);

    PacketUDPHeader* header = g_new0(PacketUDPHeader, 1);

    header->flags = flags;
    header->sourceIP = sourceIP;
    header->sourcePort = sourcePort;
    header->destinationIP = destinationIP;
    header->destinationPort = destinationPort;

    packet->header = header;
    packet->protocol = PUDP;
    _packet_unlock(packet);
}

void packet_setTCP(Packet* packet, enum ProtocolTCPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort, guint sequence) {
    _packet_lock(packet);
    utility_assert(!(packet->header) && packet->protocol == PNONE);
    utility_assert(sourceIP && sourcePort && destinationIP && destinationPort);

    PacketTCPHeader* header = g_new0(PacketTCPHeader, 1);

    header->flags = flags;
    header->sourceIP = sourceIP;
    header->sourcePort = sourcePort;
    header->destinationIP = destinationIP;
    header->destinationPort = destinationPort;
    header->sequence = sequence;

    packet->header = header;
    packet->protocol = PTCP;
    _packet_unlock(packet);
}

void packet_updateTCP(Packet* packet, guint acknowledgement, GList* selectiveACKs,
        guint window, SimulationTime timestampValue, SimulationTime timestampEcho) {
    _packet_lock(packet);
    utility_assert(packet->header && (packet->protocol == PTCP));

    PacketTCPHeader* header = (PacketTCPHeader*) packet->header;

    if(selectiveACKs && g_list_length(selectiveACKs) > 0) {
        /* free the old ack list if it exists */
        if(header->selectiveACKs != NULL) {
            g_list_free(header->selectiveACKs);
            header->selectiveACKs = NULL;
        }

        /* set the new sacks */
        header->flags |= PTCP_SACK;
        header->selectiveACKs = g_list_copy(selectiveACKs);
    }

    header->acknowledgment = acknowledgement;
    header->window = window;
    header->timestampValue = timestampValue;
    header->timestampEcho = timestampEcho;

    _packet_unlock(packet);
}

guint packet_getPayloadLength(Packet* packet) {
    /* not locked, read only */
    return packet->payloadLength;
}

gdouble packet_getPriority(Packet* packet) {
    /* not locked, read only */
    return packet->priority;
}

guint packet_getHeaderSize(Packet* packet) {
    _packet_lock(packet);
    guint size = packet->protocol == PUDP ? CONFIG_HEADER_SIZE_UDPIPETH :
            packet->protocol == PTCP ? CONFIG_HEADER_SIZE_TCPIPETH : 0;
    _packet_unlock(packet);
    return size;
}

in_addr_t packet_getDestinationIP(Packet* packet) {
    _packet_lock(packet);

    in_addr_t ip = 0;

    switch (packet->protocol) {
        case PLOCAL: {
            ip = htonl(INADDR_LOOPBACK);
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            ip = header->destinationIP;
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            ip = header->destinationIP;
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }

    _packet_unlock(packet);
    return ip;
}

in_addr_t packet_getSourceIP(Packet* packet) {
    _packet_lock(packet);

    in_addr_t ip = 0;

    switch (packet->protocol) {
        case PLOCAL: {
            ip = htonl(INADDR_LOOPBACK);
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            ip = header->sourceIP;
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            ip = header->sourceIP;
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }

    _packet_unlock(packet);
    return ip;
}

in_port_t packet_getSourcePort(Packet* packet) {
    _packet_lock(packet);

    in_port_t port = 0;

    switch (packet->protocol) {
        case PLOCAL: {
            PacketLocalHeader* header = packet->header;
            port = header->port;
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            port = header->sourcePort;
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            port = header->sourcePort;
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }

    _packet_unlock(packet);
    return port;
}

guint packet_copyPayload(Packet* packet, gsize payloadOffset, gpointer buffer, gsize bufferLength) {
    _packet_lock(packet);

    utility_assert(payloadOffset <= packet->payloadLength);

    guint targetLength = packet->payloadLength - ((guint)payloadOffset);
    guint copyLength = MIN(targetLength, bufferLength);

    if(copyLength > 0) {
        g_memmove(buffer, packet->payload + payloadOffset, copyLength);
    }

    _packet_unlock(packet);
    return copyLength;
}

gint packet_getDestinationAssociationKey(Packet* packet) {
    _packet_lock(packet);

    in_port_t port = 0;
    switch (packet->protocol) {
        case PLOCAL: {
            PacketLocalHeader* header = packet->header;
            port = header->port;
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            port = header->destinationPort;
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            port = header->destinationPort;
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }

    gint key = PROTOCOL_DEMUX_KEY(packet->protocol, port);

    _packet_unlock(packet);
    return key;
}

gint packet_getSourceAssociationKey(Packet* packet) {
    _packet_lock(packet);

    in_port_t port = 0;
    switch (packet->protocol) {
        case PLOCAL: {
            PacketLocalHeader* header = packet->header;
            port = header->port;
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            port = header->sourcePort;
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            port = header->sourcePort;
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }

    gint key = PROTOCOL_DEMUX_KEY(packet->protocol, port);

    _packet_unlock(packet);
    return key;
}

GList* packet_copyTCPSelectiveACKs(Packet* packet) {
    _packet_lock(packet);
    utility_assert(packet->protocol == PTCP);

    PacketTCPHeader* packetHeader = (PacketTCPHeader*)packet->header;

    /* make sure to do a deep copy of all pointers to avoid concurrency issues */
    GList* selectiveACKsCopy = NULL;
    if(packetHeader->selectiveACKs) {
        /* g_list_copy is shallow, but we store integers in the data pointers, so its OK here */
        selectiveACKsCopy = g_list_copy(packetHeader->selectiveACKs);
    }

    _packet_unlock(packet);

    return selectiveACKsCopy;
}

void packet_getTCPHeader(Packet* packet, PacketTCPHeader* header) {
    if(!header) {
        return;
    }

    _packet_lock(packet);

    utility_assert(packet->protocol == PTCP);

    PacketTCPHeader* packetHeader = (PacketTCPHeader*)packet->header;

    /* copy all local non-malloc'd header state */
    header->flags = packetHeader->flags;
    header->sourceIP = packetHeader->sourceIP;
    header->sourcePort = packetHeader->sourcePort;
    header->destinationIP = packetHeader->destinationIP;
    header->destinationPort = packetHeader->destinationPort;
    header->sequence = packetHeader->sequence;
    header->acknowledgment = packetHeader->acknowledgment;
    header->window = packetHeader->window;
    header->timestampValue = packetHeader->timestampValue;
    header->timestampEcho = packetHeader->timestampEcho;

    /* don't copy the selective acks list here; use packet_copyTCPSelectiveACKs for that */
    header->selectiveACKs = NULL;

    _packet_unlock(packet);
}

static const gchar* _packet_deliveryStatusToAscii(PacketDeliveryStatusFlags status) {
    switch (status) {
        case PDS_NONE: return "NONE";
        case PDS_SND_CREATED: return "SND_CREATED";
        case PDS_SND_TCP_ENQUEUE_THROTTLED: return "SND_TCP_ENQUEUE_THROTTLED";
        case PDS_SND_TCP_ENQUEUE_RETRANSMIT: return "SND_TCP_ENQUEUE_RETRANSMIT";
        case PDS_SND_TCP_DEQUEUE_RETRANSMIT: return "SND_TCP_DEQUEUE_RETRANSMIT";
        case PDS_SND_TCP_RETRANSMITTED: return "SND_TCP_RETRANSMITTED";
        case PDS_SND_SOCKET_BUFFERED: return "SND_SOCKET_BUFFERED";
        case PDS_SND_INTERFACE_SENT: return "SND_INTERFACE_SENT";
        case PDS_INET_SENT: return "INET_SENT";
        case PDS_INET_DROPPED: return "INET_DROPPED";
        case PDS_RCV_INTERFACE_BUFFERED: return "RCV_INTERFACE_BUFFERED";
        case PDS_RCV_INTERFACE_RECEIVED: return "RCV_INTERFACE_RECEIVED";
        case PDS_RCV_INTERFACE_DROPPED: return "RCV_INTERFACE_DROPPED";
        case PDS_RCV_SOCKET_PROCESSED: return "RCV_SOCKET_PROCESSED";
        case PDS_RCV_SOCKET_DROPPED: return "RCV_SOCKET_DROPPED";
        case PDS_RCV_TCP_ENQUEUE_UNORDERED: return "RCV_TCP_ENQUEUE_UNORDERED";
        case PDS_RCV_SOCKET_BUFFERED: return "RCV_SOCKET_BUFFERED";
        case PDS_RCV_SOCKET_DELIVERED: return "RCV_SOCKET_DELIVERED";
        case PDS_DESTROYED: return "PDS_DESTROYED";
        default: return "UKNOWN";
    }
}

static gchar* _packet_getString(Packet* packet) {
    GString* packetString = g_string_new("");

    //_packet_lock(packet);

    switch (packet->protocol) {
        case PLOCAL: {
            PacketLocalHeader* header = packet->header;
            g_string_append_printf(packetString, "%i -> %i bytes=%u",
                    header->sourceDescriptorHandle, header->destinationDescriptorHandle,
                    packet->payloadLength);
            break;
        }

        case PUDP: {
            PacketUDPHeader* header = packet->header;
            gchar* sourceIPString = address_ipToNewString(header->sourceIP);
            gchar* destinationIPString = address_ipToNewString(header->destinationIP);

            g_string_append_printf(packetString, "%s:%u -> ",
                    sourceIPString, ntohs(header->sourcePort));
            g_string_append_printf(packetString, "%s:%u bytes=%u",
                    destinationIPString, ntohs( header->destinationPort),
                    packet->payloadLength);

            g_free(sourceIPString);
            g_free(destinationIPString);
            break;
        }

        case PTCP: {
            PacketTCPHeader* header = packet->header;
            gchar* sourceIPString = address_ipToNewString(header->sourceIP);
            gchar* destinationIPString = address_ipToNewString(header->destinationIP);

            g_string_append_printf(packetString, "%s:%u -> ",
                    sourceIPString, ntohs(header->sourcePort));
            g_string_append_printf(packetString, "%s:%u seq=%u ack=%u sack=", 
                    destinationIPString, ntohs(header->destinationPort),
                    header->sequence, header->acknowledgment);

            // Instead of printing out entire list of SACK, print out ranges to save space
            gint firstSack = -1;
            gint lastSack = -1;
            for(GList *iter = header->selectiveACKs; iter; iter = g_list_next(iter)) {
                gint seq = GPOINTER_TO_INT(iter->data);
                if(firstSack == -1) {
                    firstSack = seq;
                } else if(lastSack == -1 || seq == lastSack + 1) {
                    lastSack = seq;
                } else {
                    g_string_append_printf(packetString, "%d-%d ", firstSack, lastSack);
                    firstSack = seq;
                    lastSack = -1;
                }
            }

            if(firstSack != -1) {
                g_string_append_printf(packetString, "%d", firstSack);
                if(lastSack != -1) {
                    g_string_append_printf(packetString,"-%d", lastSack);
                }
            } else {
                g_string_append_printf(packetString, "NA");
            }

            g_string_append_printf(packetString, " window=%u bytes=%u", header->window, packet->payloadLength);

            if(!(header->flags & PTCP_NONE)) {
                g_string_append_printf(packetString, " header=");
                if(header->flags & PTCP_RST) {
                    g_string_append_printf(packetString, "RST");
                }
                if(header->flags & PTCP_SYN) {
                    g_string_append_printf(packetString, "SYN");
                }
                if(header->flags & PTCP_FIN) {
                    g_string_append_printf(packetString, "FIN");
                }
                if(header->flags & PTCP_ACK) {
                    g_string_append_printf(packetString, "ACK");
                }
            }

            g_free(sourceIPString);
            g_free(destinationIPString);
            break;
        }

        default: {
            error("unrecognized protocol");
            break;
        }
    }
    
    g_string_append_printf(packetString, " status=");

    guint statusLength = g_queue_get_length(packet->orderedStatus);
    for(int i = 0; i < statusLength; i++) {
        gpointer statusPtr = g_queue_pop_head(packet->orderedStatus);
        PacketDeliveryStatusFlags status = (PacketDeliveryStatusFlags) GPOINTER_TO_UINT(statusPtr);

        if(i < statusLength - 1) {
            g_string_append_printf(packetString, "%s,", _packet_deliveryStatusToAscii(status));
        } else {
            g_string_append_printf(packetString, "%s", _packet_deliveryStatusToAscii(status));
        }

        g_queue_push_tail(packet->orderedStatus, statusPtr);
    }

    //_packet_unlock(packet);
    return g_string_free(packetString, FALSE);
}

void packet_addDeliveryStatus(Packet* packet, PacketDeliveryStatusFlags status) {
    gboolean skipDebug = worker_isFiltered(G_LOG_LEVEL_DEBUG);
    gchar* packetStr = NULL;

    _packet_lock(packet);
    packet->allStatus |= status;

    if(!skipDebug) {
        g_queue_push_tail(packet->orderedStatus, GUINT_TO_POINTER(status));
        packetStr = _packet_getString(packet);
    }

    _packet_unlock(packet);

    if(!skipDebug) {
        message("[%s] %s", _packet_deliveryStatusToAscii(status), packetStr);
    }
}

PacketDeliveryStatusFlags packet_getDeliveryStatus(Packet* packet) {
    _packet_lock(packet);
    PacketDeliveryStatusFlags flags = packet->allStatus;
    _packet_unlock(packet);
    return flags;
}

void packet_setDropNotificationDelay(Packet* packet, SimulationTime delay) {
    MAGIC_ASSERT(packet);
    _packet_lock(packet);
    packet->dropNotificationDelay = delay;
    _packet_unlock(packet);
}
SimulationTime packet_getDropNotificationDelay(Packet* packet) {
    MAGIC_ASSERT(packet);
    _packet_lock(packet);
    SimulationTime delay = packet->dropNotificationDelay;
    _packet_unlock(packet);
    return delay;
}
