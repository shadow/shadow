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

#ifndef SHD_NETWORK_INTERFACE_H_
#define SHD_NETWORK_INTERFACE_H_

#include "shadow.h"

typedef struct _NetworkInterface NetworkInterface;

NetworkInterface* networkinterface_new(Network* network, GQuark address, gchar* name,
		guint64 bwDownKiBps, guint64 bwUpKiBps, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
		guint64 interfaceReceiveLength);
void networkinterface_free(NetworkInterface* interface);

in_addr_t networkinterface_getIPAddress(NetworkInterface* interface);
gchar* networkinterface_getIPName(NetworkInterface* interface);
guint32 networkinterface_getSpeedUpKiBps(NetworkInterface* interface);
guint32 networkinterface_getSpeedDownKiBps(NetworkInterface* interface);

gboolean networkinterface_isAssociated(NetworkInterface* interface, gint key);
void networkinterface_associate(NetworkInterface* interface, Socket* transport);
void networkinterface_disassociate(NetworkInterface* interface, Socket* transport);

void networkinterface_packetArrived(NetworkInterface* interface, Packet* packet);
void networkinterface_received(NetworkInterface* interface);
void networkinterface_packetDropped(NetworkInterface* interface, Packet* packet);
void networkinterface_wantsSend(NetworkInterface* interface, Socket* transport);
void networkinterface_sent(NetworkInterface* interface);


#endif /* SHD_NETWORK_INTERFACE_H_ */
