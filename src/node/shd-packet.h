/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
gdouble packet_getPriority(Packet* packet);
guint packet_getHeaderSize(Packet* packet);
in_addr_t packet_getDestinationIP(Packet* packet);
in_addr_t packet_getSourceIP(Packet* packet);
in_port_t packet_getSourcePort(Packet* packet);
guint packet_copyPayload(Packet* packet, gsize payloadOffset, gpointer buffer, gsize bufferLength);
void packet_getTCPHeader(Packet* packet, PacketTCPHeader* header);
gint packet_compareTCPSequence(Packet* packet1, Packet* packet2, gpointer user_data);

gint packet_getDestinationAssociationKey(Packet* packet);
gint packet_getSourceAssociationKey(Packet* packet);

/* returned string must be freed with g_free */
gchar* packet_getString(Packet* packet);

#endif /* SHD_PACKET_H_ */
