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
    GList* selectiveACKs;
    guint window;
    unsigned char windowScale;
    bool windowScaleSet;
    CSimulationTime timestampValue;
    CSimulationTime timestampEcho;
};

Packet* packet_new(const Host* host);
void packet_setPayloadWithMemoryManager(Packet* packet, UntypedForeignPtr payload,
                                        gsize payloadLength, const MemoryManager* mem,
                                        uint64_t packetPriority);
void packet_setPayloadFromShadow(Packet* packet, const void* payload, gsize payloadLength,
                                 uint64_t packetPriority);
Packet* packet_copy(Packet* packet);

// Exposed for unit testing only. Use `packet_new` outside of tests.
Packet* packet_new_inner(guint hostID, guint64 packetID);
// For testing only; do not use outside of tests.
void packet_setMock(Packet* packet);

void packet_ref(Packet* packet);
void packet_unref(Packet* packet);

void packet_setPriority(Packet *packet, uint64_t value);
uint64_t packet_getPriority(const Packet* packet);

// The addresses and ports must be in network byte order.
void packet_setUDP(Packet* packet, ProtocolUDPFlags flags, in_addr_t sourceIP, in_port_t sourcePort,
                   in_addr_t destinationIP, in_port_t destinationPort);

// The addresses and ports must be in network byte order.
void packet_setTCP(Packet* packet, ProtocolTCPFlags flags, in_addr_t sourceIP, in_port_t sourcePort,
                   in_addr_t destinationIP, in_port_t destinationPort, guint sequence);

void packet_updateTCP(Packet* packet, guint acknowledgement, GList* selectiveACKs, guint window,
                      unsigned char windowScale, bool windowScaleSet,
                      CSimulationTime timestampValue, CSimulationTime timestampEcho);

gsize packet_getTotalSize(const Packet* packet);
gsize packet_getPayloadSize(const Packet* packet);
gsize packet_getHeaderSize(const Packet* packet);

// The returned address will be in network byte order.
in_addr_t packet_getDestinationIP(const Packet* packet);
// The returned port will be in network byte order.
in_port_t packet_getDestinationPort(const Packet* packet);

// The returned address will be in network byte order.
in_addr_t packet_getSourceIP(const Packet* packet);
// The returned port will be in network byte order.
in_port_t packet_getSourcePort(const Packet* packet);

ProtocolType packet_getProtocol(const Packet* packet);

gssize packet_copyPayloadWithMemoryManager(const Packet* packet, gsize payloadOffset,
                                           UntypedForeignPtr buffer, gsize bufferLength,
                                           MemoryManager* mem);
guint packet_copyPayloadShadow(const Packet* packet, gsize payloadOffset, void* buffer,
                               gsize bufferLength);
GList* packet_copyTCPSelectiveACKs(Packet* packet);
PacketTCPHeader* packet_getTCPHeader(const Packet* packet);
gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data);

void packet_addDeliveryStatus(Packet* packet, PacketDeliveryStatusFlags status);

#endif /* SHD_PACKET_H_ */
