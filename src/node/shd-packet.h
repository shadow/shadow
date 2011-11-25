/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHD_PACKET_H_
#define SHD_PACKET_H_

#include "shadow.h"

typedef struct _Packet Packet;

typedef struct _PacketTCPHeader PacketTCPHeader;
struct _PacketTCPHeader {
	enum ProtocolTCPFlags flags;
	in_addr_t sourceIP;
	in_port_t sourcePort;
	in_addr_t destinationIP;
	in_port_t destinationPort;
	guint sequence;
	guint acknowledgement;
	guint window;
};

Packet* packet_new(gconstpointer payload, gsize payloadLength);

void packet_ref(Packet* packet);
void packet_unref(Packet* packet);

void packet_setLocal(Packet* packet, enum ProtocolLocalFlags flags,
		gint sourceDescriptorHandle, gint destinationDescriptorHandle, in_port_t port);
void packet_setUDP(Packet* packet, enum ProtocolUDPFlags flags,
		in_addr_t sourceIP, in_port_t sourcePort,
		in_addr_t destinationIP, in_port_t destinationPort);
void packet_setTCP(Packet* packet, enum ProtocolTCPFlags flags,
		in_addr_t sourceIP, in_port_t sourcePort,
		in_addr_t destinationIP, in_port_t destinationPort,
		guint sequence, guint acknowledgement, guint window);

void packet_updateTCP(Packet* packet, guint acknowledgement, guint window);

guint packet_getPayloadLength(Packet* packet);
guint packet_getHeaderSize(Packet* packet);
in_addr_t packet_getDestinationIP(Packet* packet);
in_addr_t packet_getSourceIP(Packet* packet);
in_port_t packet_getSourcePort(Packet* packet);
guint packet_copyPayload(Packet* packet, gsize payloadOffset, gpointer buffer, gsize bufferLength);
gint packet_getAssociationKey(Packet* packet);
void packet_getTCPHeader(Packet* packet, PacketTCPHeader* header);
gint packet_compareTCPSequence(const Packet* packet1, const Packet* packet2, gpointer user_data);

#endif /* SHD_PACKET_H_ */
