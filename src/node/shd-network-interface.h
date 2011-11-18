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
struct _NetworkInterface {
	Address* address;
	guint32 bwDownKiBps;
	guint32 bwUpKiBps;
	/* port-to-descriptor bindings */
	GHashTable* boundTCP;
	GHashTable* boundUDP;
	MAGIC_DECLARE;
};

NetworkInterface* networkinterface_new(GQuark address, gchar* name,
		guint32 bwDownKiBps, guint32 bwUpKiBps);
void networkinterface_free(gpointer data);

in_addr_t networkinterface_getIPAddress(NetworkInterface* interface);
gboolean networkinterface_isAssociated(NetworkInterface* interface,
		enum DescriptorType type, in_port_t port);
void networkinterface_associate(NetworkInterface* interface, Socket* socket);

#endif /* SHD_NETWORK_INTERFACE_H_ */
