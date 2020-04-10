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

#ifndef SHD_PCAP_WRITER_H_
#define SHD_PCAP_WRITER_H_

#include <glib.h>
#include <netinet/in.h>

typedef struct _PCapWriter PCapWriter;

typedef struct _PCapPacket PCapPacket;
struct _PCapPacket {
    in_addr_t srcIP;
    in_addr_t dstIP;
    in_port_t srcPort;
    in_port_t dstPort;
    gboolean rstFlag;
    gboolean synFlag;
    gboolean ackFlag;
    gboolean finFlag;
    guint32 seq;
    guint32 ack;
    guint16 win;
    guint headerSize;
    guint payloadLength;
    gpointer payload;
};

PCapWriter* pcapwriter_new(gchar* pcapDirectory, gchar* pcapFilename);
void pcapwriter_free(PCapWriter* pcap);
void pcapwriter_writePacket(PCapWriter* pcap, PCapPacket* packet);

#endif /* SHD_PCAP_WRITER_H_ */
