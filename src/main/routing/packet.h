/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_H_
#define SHD_PACKET_H_

#include <netinet/in.h>

#include "main/routing/packet.minimal.h"

#include "main/bindings/c/bindings-opaque.h"
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

    unsigned int sequence;
    unsigned int acknowledgment;
    PacketSelectiveAcks selectiveACKs;
    unsigned int window;
    unsigned char windowScale;
    bool windowScaleSet;
    CSimulationTime timestampValue;
    CSimulationTime timestampEcho;
};

#endif /* SHD_PACKET_H_ */
