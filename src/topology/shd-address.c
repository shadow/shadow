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
	address->ipString = address_ipToNewString((in_addr_t)ip); // XXX do we need to use ntohl() first?
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

gchar* address_ipToNewString(in_addr_t ip) {
	gchar* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN+1);
	const gchar* ipString = inet_ntop(AF_INET, &ip, ipStringBuffer, INET6_ADDRSTRLEN);
	GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
	g_free(ipStringBuffer);
	return g_string_free(result, FALSE);
}
