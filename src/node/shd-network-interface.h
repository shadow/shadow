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

#ifndef SHD_NETWORK_INTERFACE_H_
#define SHD_NETWORK_INTERFACE_H_

#include "shadow.h"

typedef struct _NetworkInterface NetworkInterface;

NetworkInterface* networkinterface_new(Network* network, GQuark address, gchar* name,
		guint64 bwDownKiBps, guint64 bwUpKiBps);
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
