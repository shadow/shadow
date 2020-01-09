/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_H_
#define SHD_PACKET_H_

#include "shadow.h"

typedef struct _Packet Packet;

typedef enum _PacketDeliveryStatusFlags PacketDeliveryStatusFlags;
enum _PacketDeliveryStatusFlags {
    PDS_NONE = 0,
    PDS_SND_CREATED = 1 << 1,
    PDS_SND_TCP_ENQUEUE_THROTTLED = 1 << 2,
    PDS_SND_TCP_ENQUEUE_RETRANSMIT = 1 << 3,
    PDS_SND_TCP_DEQUEUE_RETRANSMIT = 1 << 4,
    PDS_SND_TCP_RETRANSMITTED = 1 << 5,
    PDS_SND_SOCKET_BUFFERED = 1 << 6,
    PDS_SND_INTERFACE_SENT = 1 << 7,
    PDS_INET_SENT = 1 << 8,
    PDS_INET_DROPPED = 1 << 9,
    PDS_ROUTER_ENQUEUED = 1 << 10,
    PDS_ROUTER_DEQUEUED = 1 << 11,
    PDS_ROUTER_DROPPED = 1 << 12,
    PDS_RCV_INTERFACE_RECEIVED = 1 << 13,
    PDS_RCV_INTERFACE_DROPPED = 1 << 14,
    PDS_RCV_SOCKET_PROCESSED = 1 << 15,
    PDS_RCV_SOCKET_DROPPED = 1 << 16,
    PDS_RCV_TCP_ENQUEUE_UNORDERED = 1 << 17,
    PDS_RCV_SOCKET_BUFFERED = 1 << 18,
    PDS_RCV_SOCKET_DELIVERED = 1 << 19,
    PDS_DESTROYED = 1 << 20,
};

typedef struct _PacketTCPHeader PacketTCPHeader;
struct _PacketTCPHeader {
    enum ProtocolTCPFlags flags;
    in_addr_t sourceIP;
    in_port_t sourcePort;
    in_addr_t destinationIP;
    in_port_t destinationPort;
    guint sequence;
    guint acknowledgment;
    GList* selectiveACKs;
    guint window;
    SimulationTime timestampValue;
    SimulationTime timestampEcho;
};

const gchar* protocol_toString(ProtocolType type);

Packet* packet_new(gconstpointer payload, gsize payloadLength, guint hostID, guint64 packetID);
Packet* packet_copy(Packet* packet);

void packet_ref(Packet* packet);
void packet_unref(Packet* packet);

void packet_setPriority(Packet *packet, double value);

void packet_setLocal(Packet* packet, enum ProtocolLocalFlags flags,
        gint sourceDescriptorHandle, gint destinationDescriptorHandle, in_port_t port);
void packet_setUDP(Packet* packet, enum ProtocolUDPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort);
void packet_setTCP(Packet* packet, enum ProtocolTCPFlags flags,
        in_addr_t sourceIP, in_port_t sourcePort,
        in_addr_t destinationIP, in_port_t destinationPort, guint sequence);

void packet_updateTCP(Packet* packet, guint acknowledgement, GList* selectiveACKs,
        guint window, SimulationTime timestampValue, SimulationTime timestampEcho);

guint packet_getPayloadLength(Packet* packet);
gdouble packet_getPriority(Packet* packet);
guint packet_getHeaderSize(Packet* packet);

in_addr_t packet_getDestinationIP(Packet* packet);
in_port_t packet_getDestinationPort(Packet* packet);
in_addr_t packet_getSourceIP(Packet* packet);
in_port_t packet_getSourcePort(Packet* packet);
ProtocolType packet_getProtocol(Packet* packet);

guint packet_copyPayload(Packet* packet, gsize payloadOffset, gpointer buffer, gsize bufferLength);
GList* packet_copyTCPSelectiveACKs(Packet* packet);
PacketTCPHeader* packet_getTCPHeader(Packet* packet);
gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data);

void packet_addDeliveryStatus(Packet* packet, PacketDeliveryStatusFlags status);
PacketDeliveryStatusFlags packet_getDeliveryStatus(Packet* packet);

gchar* packet_toString(Packet* packet);

#endif /* SHD_PACKET_H_ */
