/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
