/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/worker.h"
#include "main/routing/address.h"
#include "main/routing/packet.h"
#include "main/routing/payload.h"
#include "main/utility/utility.h"

/* g_memdup() is deprecated due to a security issue and has been replaced
 * by g_memdup2(), but not all of our supported platforms support this yet.
 * https://gitlab.gnome.org/GNOME/glib/-/issues/2319
 *
 * Instead we'll use our own version.
 *
 * https://docs.gtk.org/glib/func.memdup2.html
 * > Allocates byte_size bytes of memory, and copies byte_size bytes into it
 * > from mem. If mem is NULL it returns NULL.
 */

void* memdup(const void* ptr, size_t byteSize) {
    if (ptr == NULL) {
        return NULL;
    }

    void* newPtr = malloc(byteSize);
    utility_alwaysAssert(newPtr != NULL);

    memcpy(newPtr, ptr, byteSize);
    return newPtr;
}

/* thread-safe structure representing a data/network packet */

typedef struct _PacketUDPHeader PacketUDPHeader;
struct _PacketUDPHeader {
    enum ProtocolUDPFlags flags;

    // address is in network byte order
    in_addr_t sourceIP;
    // port is in network byte order
    in_port_t sourcePort;

    // address is in network byte order
    in_addr_t destinationIP;
    // port is in network byte order
    in_port_t destinationPort;
};

/* packets are guaranteed not to be shared across hosts */
struct _Packet {
    guint referenceCount;

    /* id of the host that created the packet */
    guint hostID;
    /* id of the packet created on the host given by hostID */
    guint64 packetID;

    ProtocolType protocol;
    gpointer header;
    Payload* payload;

    /* tracks application priority so we flush packets from the interface to
     * the wire in the order intended by the application. this is used in
     * the default FIFO network interface scheduling discipline.
     * smaller values have greater priority.
     */
    uint64_t priority;

    PacketDeliveryStatusFlags allStatus;
    GQueue* orderedStatus;

    MAGIC_DECLARE;
};

const gchar* protocol_toString(ProtocolType type) {
    switch (type) {
        case PUDP: return "UDP";
        case PTCP: return "TCP";
        case PMOCK: return "MOCK";
        default: return "UNKNOWN";
    }
}

// Exposed for unit testing only. Use `packet_new` outside of tests.
Packet* packet_new_inner(guint hostID, guint64 packetID) {
    Packet* packet = g_new0(Packet, 1);
    MAGIC_INIT(packet);

    packet->referenceCount = 1;

    packet->hostID = hostID;
    packet->packetID = packetID;

    packet->orderedStatus = g_queue_new();

    return packet;
}

Packet* packet_new(const Host* host) {
    guint hostID = host_getID(host);
    guint64 packetID = host_getNewPacketID(host);
    Packet* packet = packet_new_inner(hostID, packetID);
    worker_count_allocation(Packet);
    return packet;
}

/* If modifying this function, you should also modify `packet_setPayloadWithMemoryManager` below.
 */
void packet_setPayload(Packet* packet, const Thread* thread, UntypedForeignPtr payload,
                       gsize payloadLength, uint64_t packetPriority) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(thread);
    utility_debugAssert(payload.val);
    utility_debugAssert(!packet->payload);

    /* the payload starts with 1 ref, which we hold */
    packet->payload = payload_new(thread, payload, payloadLength);
    utility_alwaysAssert(packet->payload != NULL);
    /* application data needs a priority ordering for FIFO onto the wire */
    packet->priority = packetPriority;
}

/* This is a copy of `packet_setPayload` but passes the memory manager through. Once we've moved UDP
 * sockets to rust, we can remove `packet_setPayload` and rename this function to
 * `packet_setPayload`. */
void packet_setPayloadWithMemoryManager(Packet* packet, UntypedForeignPtr payload,
                                        gsize payloadLength, const MemoryManager* mem,
                                        uint64_t packetPriority) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(payload.val);
    utility_debugAssert(!packet->payload);

    /* the payload starts with 1 ref, which we hold */
    packet->payload = payload_newWithMemoryManager(payload, payloadLength, mem);
    utility_alwaysAssert(packet->payload != NULL);
    /* application data needs a priority ordering for FIFO onto the wire */
    packet->priority = packetPriority;
}

void packet_setPayloadFromShadow(Packet* packet, const void* payload, gsize payloadLength,
                                 uint64_t packetPriority) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(payload);
    utility_debugAssert(!packet->payload);

    /* the payload starts with 1 ref, which we hold */
    packet->payload = payload_newFromShadow(payload, payloadLength);
    utility_alwaysAssert(packet->payload != NULL);
    /* application data needs a priority ordering for FIFO onto the wire */
    packet->priority = packetPriority;
}

/* copy everything except the payload.
 * the payload will point to the same payload as the original packet.
 * the payload is protected so it is safe to send the copied packet to a different host. */
Packet* packet_copy(Packet* packet) {
    MAGIC_ASSERT(packet);

    Packet* copy = g_new0(Packet, 1);
    MAGIC_INIT(copy);

    copy->referenceCount = 1;

    copy->hostID = packet->hostID;
    copy->packetID = packet->packetID;

    if(packet->payload) {
        copy->payload = packet->payload;
        payload_ref(packet->payload);
        copy->priority = packet->priority;
    }

    copy->allStatus = packet->allStatus;

    if(packet->orderedStatus) {
        /* this is ok because we store ints in the pointers, not objects */
        copy->orderedStatus = g_queue_copy(packet->orderedStatus);
    }

    copy->protocol = packet->protocol;
    if(packet->header) {
        switch (packet->protocol) {
            case PUDP: {
                copy->header = memdup(packet->header, sizeof(PacketUDPHeader));
                break;
            }

            case PTCP: {
                copy->header = memdup(packet->header, sizeof(PacketTCPHeader));

                PacketTCPHeader* packetHeader = (PacketTCPHeader*)packet->header;
                PacketTCPHeader* copyHeader = (PacketTCPHeader*)copy->header;

                copyHeader->selectiveACKs = NULL;

                if(packetHeader->selectiveACKs) {
                    /* g_list_copy is shallow, but we store integers in the data pointers, so its OK here */
                    copyHeader->selectiveACKs = g_list_copy(packetHeader->selectiveACKs);
                }
                break;
            }

            default: {
                utility_panic("unrecognized protocol");
                break;
            }
        }
    }

    worker_count_allocation(Packet);
    return copy;
}

static void _packet_free(Packet* packet) {
    MAGIC_ASSERT(packet);

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
        payload_unref(packet->payload);
    }
    if(packet->orderedStatus) {
        g_queue_free(packet->orderedStatus);
    }

    MAGIC_CLEAR(packet);
    g_free(packet);

    worker_count_deallocation(Packet);
}

void packet_ref(Packet* packet) {
    MAGIC_ASSERT(packet);
    (packet->referenceCount)++;
}

void packet_unref(Packet* packet) {
    MAGIC_ASSERT(packet);
    (packet->referenceCount)--;
    if(packet->referenceCount == 0) {
        packet_addDeliveryStatus(packet, PDS_DESTROYED);
        _packet_free(packet);
    }
}

void packet_setPriority(Packet *packet, uint64_t value) {
   packet->priority = value;
}

gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data) {
    MAGIC_ASSERT(packet1);
    MAGIC_ASSERT(packet2);

    /* packet1 for one worker might be packet2 for another, dont lock both
     * at once or a deadlock will occur */
    guint sequence1 = 0, sequence2 = 0;

    utility_debugAssert(packet1->protocol == PTCP);
    sequence1 = ((PacketTCPHeader*)(packet1->header))->sequence;

    utility_debugAssert(packet2->protocol == PTCP);
    sequence2 = ((PacketTCPHeader*)(packet2->header))->sequence;

    return sequence1 < sequence2 ? -1 : sequence1 > sequence2 ? 1 : 0;
}

// Enables non-zero size for mock packets for testing. Do not use outside of testing.
void packet_setMock(Packet* packet) {
    MAGIC_ASSERT(packet);
    packet->protocol = PMOCK;
}

// The addresses and ports must be in network byte order.
void packet_setUDP(Packet* packet, enum ProtocolUDPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(!(packet->header) && packet->protocol == PNONE);
    utility_debugAssert(sourceIP && sourcePort && destinationIP && destinationPort);

    PacketUDPHeader* header = g_new0(PacketUDPHeader, 1);

    header->flags = flags;
    header->sourceIP = sourceIP;
    header->sourcePort = sourcePort;
    header->destinationIP = destinationIP;
    header->destinationPort = destinationPort;

    packet->header = header;
    packet->protocol = PUDP;
}

// The addresses and ports must be in network byte order.
void packet_setTCP(Packet* packet, enum ProtocolTCPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort, guint sequence) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(!(packet->header) && packet->protocol == PNONE);
    utility_debugAssert(sourceIP && sourcePort && destinationIP && destinationPort);

    PacketTCPHeader* header = g_new0(PacketTCPHeader, 1);

    header->flags = flags;
    header->sourceIP = sourceIP;
    header->sourcePort = sourcePort;
    header->destinationIP = destinationIP;
    header->destinationPort = destinationPort;
    header->sequence = sequence;

    packet->header = header;
    packet->protocol = PTCP;
}

void packet_updateTCP(Packet* packet, guint acknowledgement, GList* selectiveACKs, guint window,
                      unsigned char windowScale, bool windowScaleSet,
                      CSimulationTime timestampValue, CSimulationTime timestampEcho) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(packet->header && (packet->protocol == PTCP));

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
    header->windowScale = windowScale;
    header->windowScaleSet = windowScaleSet;
    header->timestampValue = timestampValue;
    header->timestampEcho = timestampEcho;
}

gsize packet_getTotalSize(const Packet* packet) {
    MAGIC_ASSERT(packet);
    return packet_getPayloadSize(packet) + packet_getHeaderSize(packet);
}

gsize packet_getPayloadSize(const Packet* packet) {
    MAGIC_ASSERT(packet);
    if (packet->protocol == PMOCK) {
        return CONFIG_MTU;
    } else if (packet->payload) {
        return payload_getLength(packet->payload);
    } else {
        return 0;
    }
}

gsize packet_getHeaderSize(const Packet* packet) {
    MAGIC_ASSERT(packet);

    if (packet->protocol == PUDP) {
        return CONFIG_HEADER_SIZE_UDPIP;
    } else if (packet->protocol == PTCP) {
        gsize size = CONFIG_HEADER_SIZE_TCPIP;

        // tcp options use additional bytes
        PacketTCPHeader* header = packet_getTCPHeader(packet);
        utility_alwaysAssert(header != NULL);
        if (header->windowScaleSet) {
            // window scale option is 3 bytes
            size += 3;
        }

        // add padding bytes if needed
        if ((size % 4) != 0) {
            size += 4 - (size % 4);
        }

        return size;
    } else {
        return 0;
    }
}

uint64_t packet_getPriority(const Packet* packet) {
    MAGIC_ASSERT(packet);
    return packet->priority;
}

// The returned address will be in network byte order.
in_addr_t packet_getDestinationIP(const Packet* packet) {
    MAGIC_ASSERT(packet);
    in_addr_t ip = 0;

    switch (packet->protocol) {
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
            utility_panic("unrecognized protocol");
            break;
        }
    }

    return ip;
}

// The returned port will be in network byte order.
in_port_t packet_getDestinationPort(const Packet* packet) {
    MAGIC_ASSERT(packet);

    in_port_t port = 0;

    switch (packet->protocol) {
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
            utility_panic("unrecognized protocol");
            break;
        }
    }

    return port;
}

// The returned address will be in network byte order.
in_addr_t packet_getSourceIP(const Packet* packet) {
    MAGIC_ASSERT(packet);

    in_addr_t ip = 0;

    switch (packet->protocol) {
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
            utility_panic("unrecognized protocol");
            break;
        }
    }

    return ip;
}

// The returned port will be in network byte order.
in_port_t packet_getSourcePort(const Packet* packet) {
    MAGIC_ASSERT(packet);

    in_port_t port = 0;

    switch (packet->protocol) {
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
            utility_panic("unrecognized protocol");
            break;
        }
    }

    return port;
}

ProtocolType packet_getProtocol(const Packet* packet) {
    MAGIC_ASSERT(packet);
    return packet->protocol;
}

/* If modifying this function, you should also modify `packet_copyPayloadWithMemoryManager` below.
 */
gssize packet_copyPayload(const Packet* packet, const Thread* thread, gsize payloadOffset,
                          UntypedForeignPtr buffer, gsize bufferLength) {
    MAGIC_ASSERT(packet);

    if(packet->payload) {
        return payload_getData(packet->payload, thread, payloadOffset, buffer, bufferLength);
    } else {
        return 0;
    }
}

/* This is a copy of `packet_copyPayload` but passes the memory manager through. Once we've moved
 * UDP sockets to rust, we can remove `packet_copyPayload` and rename this function to
 * `packet_copyPayload`. */
gssize packet_copyPayloadWithMemoryManager(const Packet* packet, gsize payloadOffset,
                                           UntypedForeignPtr buffer, gsize bufferLength,
                                           MemoryManager* mem) {
    MAGIC_ASSERT(packet);

    if(packet->payload) {
        return payload_getDataWithMemoryManager(
            packet->payload, payloadOffset, buffer, bufferLength, mem);
    } else {
        return 0;
    }
}

guint packet_copyPayloadShadow(const Packet* packet, gsize payloadOffset, void* buffer,
                               gsize bufferLength) {
    MAGIC_ASSERT(packet);

    if (packet->payload) {
        return payload_getDataShadow(packet->payload, payloadOffset, buffer, bufferLength);
    } else {
        return 0;
    }
}

GList* packet_copyTCPSelectiveACKs(Packet* packet) {
    MAGIC_ASSERT(packet);
    utility_debugAssert(packet->protocol == PTCP);

    PacketTCPHeader* packetHeader = (PacketTCPHeader*)packet->header;

    /* make sure to do a deep copy of all pointers to avoid concurrency issues */
    GList* selectiveACKsCopy = NULL;
    if(packetHeader->selectiveACKs) {
        /* g_list_copy is shallow, but we store integers in the data pointers, so its OK here */
        selectiveACKsCopy = g_list_copy(packetHeader->selectiveACKs);
    }

    return selectiveACKsCopy;
}

PacketTCPHeader* packet_getTCPHeader(const Packet* packet) {
    MAGIC_ASSERT(packet);
    utility_alwaysAssert(packet->protocol == PTCP);
    return (PacketTCPHeader*)packet->header;
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
        case PDS_ROUTER_ENQUEUED: return "ROUTER_ENQUEUED";
        case PDS_ROUTER_DEQUEUED: return "ROUTER_DEQUEUED";
        case PDS_ROUTER_DROPPED: return "ROUTER_DROPPED";
        case PDS_RCV_INTERFACE_RECEIVED: return "RCV_INTERFACE_RECEIVED";
        case PDS_RCV_INTERFACE_DROPPED: return "RCV_INTERFACE_DROPPED";
        case PDS_RCV_SOCKET_PROCESSED: return "RCV_SOCKET_PROCESSED";
        case PDS_RCV_SOCKET_DROPPED: return "RCV_SOCKET_DROPPED";
        case PDS_RCV_TCP_ENQUEUE_UNORDERED: return "RCV_TCP_ENQUEUE_UNORDERED";
        case PDS_RCV_SOCKET_BUFFERED: return "RCV_SOCKET_BUFFERED";
        case PDS_RCV_SOCKET_DELIVERED: return "RCV_SOCKET_DELIVERED";
        case PDS_DESTROYED: return "PDS_DESTROYED";
        case PDS_RELAY_CACHED: return "RELAY_CACHED";
        case PDS_RELAY_FORWARDED: return "RELAY_FORWARDED";
        default: return "UKNOWN";
    }
}

gchar* packet_toString(Packet* packet) {
    MAGIC_ASSERT(packet);
    GString* packetString = g_string_new("");

    g_string_append_printf(packetString, "packetID=%u:%"G_GUINT64_FORMAT" ",
            packet->hostID, packet->packetID);

    guint payloadLength = (packet->payload) ? (guint)payload_getLength(packet->payload) : 0;

    switch (packet->protocol) {
        case PUDP: {
            PacketUDPHeader* header = packet->header;
            gchar* sourceIPString = address_ipToNewString(header->sourceIP);
            gchar* destinationIPString = address_ipToNewString(header->destinationIP);

            g_string_append_printf(packetString, "%s:%u -> ",
                    sourceIPString, ntohs(header->sourcePort));
            g_string_append_printf(packetString, "%s:%u bytes=%u",
                    destinationIPString, ntohs( header->destinationPort),
                    payloadLength);

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

            g_string_append_printf(packetString, " window=%u bytes=%u", header->window, payloadLength);

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
                if(header->flags & PTCP_DUPACK) {
                    g_string_append_printf(packetString, "DUPACK");
                }
            }

            g_string_append_printf(packetString, " tsval=%"G_GUINT64_FORMAT" tsechoreply=%"G_GUINT64_FORMAT,
                    header->timestampValue, header->timestampEcho);

            g_free(sourceIPString);
            g_free(destinationIPString);
            break;
        }

        case PMOCK: {
            // TODO: We should panic here if this isn't a test.  We don't have a
            // good way to check whether this is being run inside a test in C.
            g_string_append_printf(packetString, "<PMOCK>");
            break;
        }

        default: {
            utility_panic("unrecognized protocol");
            break;
        }
    }

    guint statusLength = g_queue_get_length(packet->orderedStatus);
    if(statusLength > 0) {
        g_string_append_printf(packetString, " status=");
    }
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

    return g_string_free(packetString, FALSE);
}

gchar* _packet_getString(Packet* packet) {
    return packet_toString(packet);
}

void packet_addDeliveryStatus(Packet* packet, PacketDeliveryStatusFlags status) {
    MAGIC_ASSERT(packet);

    packet->allStatus |= status;

    if (logger_isEnabled(logger_getDefault(), LOGLEVEL_TRACE)) {
        g_queue_push_tail(packet->orderedStatus, GUINT_TO_POINTER(status));
        gchar* packetStr = packet_toString(packet);
        trace("[%s] %s", _packet_deliveryStatusToAscii(status), packetStr);
        g_free(packetStr);
    }
}

PacketDeliveryStatusFlags packet_getDeliveryStatus(Packet* packet) {
    MAGIC_ASSERT(packet);
    return packet->allStatus;
}
