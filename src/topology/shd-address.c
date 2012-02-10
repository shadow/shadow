/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

/*
 * host order INADDR_LOOPBACK: 2130706433, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 * network order INADDR_LOOPBACK: 16777343, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 */

/* IP must be first so we can cast an Address to an in_addr_t */
struct _Address {
	/* the IP in network-order */
	guint32 ip;

	/* the host-order IP in dots-and-decimals format */
	gchar* ipString;

	/* the hostname */
	gchar* name;

	MAGIC_DECLARE;
};

Address* address_new(guint32 ip, const gchar* name) {
	Address* address = g_new0(Address, 1);
	MAGIC_INIT(address);

	address->ip = ip;
	address->ipString = g_strdup(NTOA(ip)); // XXX do we need to use ntohl() first?
	address->name = g_strdup(name);

	return address;
}

void address_free(Address* address) {
	MAGIC_ASSERT(address);

	g_free(address->ipString);
	g_free(address->name);

	MAGIC_CLEAR(address);
	g_free(address);
}

gboolean address_isEqual(Address* a, Address* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		MAGIC_ASSERT(a);
		MAGIC_ASSERT(b);
		return a->ip == b->ip;
	}
}

guint32 address_toHostIP(Address* address) {
	MAGIC_ASSERT(address);
	return ntohl(address->ip);
}

gchar* address_toHostIPString(Address* address) {
	MAGIC_ASSERT(address);
	return address->ipString;
}

guint32 address_toNetworkIP(Address* address) {
	MAGIC_ASSERT(address);
	return address->ip;
}

gchar* address_toHostName(Address* address) {
	MAGIC_ASSERT(address);
	return address->name;
}
