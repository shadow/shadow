/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_H_
#define SHD_PACKET_H_

#include <glib.h>
#include <netinet/in.h>

#include "main/routing/packet.minimal.h"

#include "main/bindings/c/bindings-opaque.h"
#include "main/core/definitions.h"
#include "main/host/protocol.h"

typedef struct _PacketTCPHeader PacketTCPHeader;
struct _PacketTCPHeader {
    ProtocolTCPFlags flags;

    // address is in network byte order
    in_addr_t sourceIP;
    // port is in network byte order
    in_port_t sourcePort;

    // address is in network byte order
    in_addr_t destinationIP;
    // port is in network byte order
    in_port_t destinationPort;

    guint sequence;
    guint acknowledgment;
    PacketSelectiveAcks selectiveACKs;
    guint window;
    unsigned char windowScale;
    bool windowScaleSet;
    CSimulationTime timestampValue;
    CSimulationTime timestampEcho;
};

Packet* legacypacket_new(const Host* host);
void legacypacket_setPayloadWithMemoryManager(Packet* packet, UntypedForeignPtr payload,
                                              gsize payloadLength, const MemoryManager* mem,
                                              uint64_t packetPriority);
void legacypacket_setPayloadFromShadow(Packet* packet, const void* payload, gsize payloadLength,
                                       uint64_t packetPriority);
Packet* legacypacket_copy(Packet* packet);

// Exposed for unit testing only. Use `legacypacket_new` outside of tests.
Packet* legacypacket_new_inner(guint hostID, guint64 packetID);
// For testing only; do not use outside of tests.
void legacypacket_setMock(Packet* packet);

void legacypacket_ref(Packet* packet);
void legacypacket_unref(Packet* packet);

void legacypacket_setPriority(Packet* packet, uint64_t value);
uint64_t legacypacket_getPriority(const Packet* packet);

// The addresses and ports must be in network byte order.
void legacypacket_setUDP(Packet* packet, ProtocolUDPFlags flags, in_addr_t sourceIP,
                         in_port_t sourcePort, in_addr_t destinationIP, in_port_t destinationPort);

// The addresses and ports must be in network byte order.
void legacypacket_setTCP(Packet* packet, ProtocolTCPFlags flags, in_addr_t sourceIP,
                         in_port_t sourcePort, in_addr_t destinationIP, in_port_t destinationPort,
                         guint sequence);

void legacypacket_updateTCP(Packet* packet, guint acknowledgement,
                            PacketSelectiveAcks selectiveACKs, guint window,
                            unsigned char windowScale, bool windowScaleSet,
                            CSimulationTime timestampValue, CSimulationTime timestampEcho);

gsize legacypacket_getTotalSize(const Packet* packet);
gsize legacypacket_getPayloadSize(const Packet* packet);
gsize legacypacket_getHeaderSize(const Packet* packet);

// The returned address will be in network byte order.
in_addr_t legacypacket_getDestinationIP(const Packet* packet);
// The returned port will be in network byte order.
in_port_t legacypacket_getDestinationPort(const Packet* packet);

// The returned address will be in network byte order.
in_addr_t legacypacket_getSourceIP(const Packet* packet);
// The returned port will be in network byte order.
in_port_t legacypacket_getSourcePort(const Packet* packet);

ProtocolType legacypacket_getProtocol(const Packet* packet);

gssize legacypacket_copyPayloadWithMemoryManager(const Packet* packet, gsize payloadOffset,
                                                 UntypedForeignPtr buffer, gsize bufferLength,
                                                 MemoryManager* mem);
guint legacypacket_copyPayloadShadow(const Packet* packet, gsize payloadOffset, void* buffer,
                                     gsize bufferLength);
PacketTCPHeader* legacypacket_getTCPHeader(const Packet* packet);
gint legacypacket_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data);

void legacypacket_addDeliveryStatus(Packet* packet, PacketDeliveryStatusFlags status);

#endif /* SHD_PACKET_H_ */
