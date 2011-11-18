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

#include "shadow.h"

NetworkInterface* networkinterface_new(GQuark address, gchar* name,
		guint32 bwDownKiBps, guint32 bwUpKiBps) {
	NetworkInterface* interface = g_new0(NetworkInterface, 1);
	MAGIC_INIT(interface);

	interface->address = address_new(address, (const gchar*) name);
	interface->bwDownKiBps = bwDownKiBps;
	interface->bwUpKiBps = bwUpKiBps;
	interface->boundTCP = g_hash_table_new_full(g_int16_hash, g_int16_equal, NULL, descriptor_unref);
	interface->boundUDP = g_hash_table_new_full(g_int16_hash, g_int16_equal, NULL, descriptor_unref);

	char buffer[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &address, buffer, INET_ADDRSTRLEN);

	debug("bringing up network interface '%s' at '%s', %u KiBps up and %u KiBps down",
			name, buffer, bwUpKiBps, bwDownKiBps);

	return interface;
}

void networkinterface_free(gpointer data) {
	NetworkInterface* interface = data;
	MAGIC_ASSERT(interface);

	address_free(interface->address);
	g_hash_table_destroy(interface->boundTCP);
	g_hash_table_destroy(interface->boundUDP);

	MAGIC_CLEAR(interface);
	g_free(interface);
}

gboolean networkinterface_isAssociated(NetworkInterface* interface,
		enum DescriptorType type, in_port_t port) {
	MAGIC_ASSERT(interface);

	GHashTable* usedPorts = type == DT_TCPSOCKET ? interface->boundTCP :
			type == DT_UDPSOCKET ? interface->boundUDP : NULL;

	if(!usedPorts) {
		error("descriptor type unsupported");
	}

	if(g_hash_table_lookup(usedPorts, &port)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

void networkinterface_associate(NetworkInterface* interface, Socket* socket) {
	MAGIC_ASSERT(interface);

	/* double check our logic */
	g_assert(socket_isBound(socket));
	g_assert(interface->address->ip == socket->boundInterfaceIP);
	enum DescriptorType type = descriptor_getType((Descriptor*) socket);
	g_assert(!networkinterface_isAssociated(interface, type, socket->boundPort));

	GHashTable* protocolTable = type == DT_TCPSOCKET ? interface->boundTCP :
				type == DT_UDPSOCKET ? interface->boundUDP : NULL;

	if(!protocolTable) {
		error("descriptor type unsupported");
	}

	g_hash_table_replace(protocolTable, &(socket->boundPort), socket);
	descriptor_ref(socket);
}

in_addr_t networkinterface_getIPAddress(NetworkInterface* interface) {
	MAGIC_ASSERT(interface);
	return address_toNetworkIP(interface->address);
}
