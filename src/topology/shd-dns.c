/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _DNS {
	in_addr_t ipAddressCounter;
	guint macAddressCounter;

	/* address mappings */
	GHashTable* addressByIP;
	GHashTable* addressByName;

	MAGIC_DECLARE;
};

static gboolean _dns_isRestricted(DNS* dns, in_addr_t ip) {
	/* FIXME: there are many more restricted IP ranges
	 * e.g. 192.168..., 10.0.0.0/8, etc.
	 * there is an RFC that defines these.
	 */
	if(ip == htonl(INADDR_NONE) || ip == htonl(INADDR_ANY) ||
			ip == htonl(INADDR_LOOPBACK) || ip == htonl(INADDR_BROADCAST)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static gboolean _dns_isIPUnique(DNS* dns, in_addr_t ip) {
	return dns_resolveIPToName(dns, (guint32) ip) == NULL;
}

static in_addr_t _dns_generateIP(DNS* dns) {
	MAGIC_ASSERT(dns);

	in_addr_t ip = htonl(++dns->ipAddressCounter);

	while(_dns_isRestricted(dns, ip) || !_dns_isIPUnique(dns, ip)) {
		ip = htonl(++dns->ipAddressCounter);
	}

	return ip;
}

Address* dns_register(DNS* dns, GQuark id, gchar* name, gchar* requestedIP) {
	MAGIC_ASSERT(dns);
	g_assert(name);

	in_addr_t ip = 0;
	guint mac = ++dns->macAddressCounter;

	/* if requestedIP is NULL, we should generate one ourselves */
	if(requestedIP) {
		struct in_addr inaddr;
		if(1 == inet_pton(AF_INET, requestedIP, &inaddr)) {
			ip = inaddr.s_addr;
			/* restricted is OK if this is a localhost address, otherwise it must be unique */
			if(!_dns_isRestricted(dns, ip) && !_dns_isIPUnique(dns, ip)) {
				ip = _dns_generateIP(dns);
			}
		} else {
			error("inet_pton: error converting requested IP address '%s' to network IP address", requestedIP);
		}
	} else {
		ip = _dns_generateIP(dns);
	}

	gboolean isLocal = _dns_isRestricted(dns, ip);
	Address* address = address_new(mac, (guint32) ip, name, isLocal);

	/* store the ip/name mappings */
	if(!isLocal) {
		g_hash_table_replace(dns->addressByIP, GUINT_TO_POINTER(address_toHostIP(address)), address);
		address_ref(address);
		g_hash_table_replace(dns->addressByName, address_toHostName(address), address);
		address_ref(address);
	}

	return address;
}

void dns_deregister(DNS* dns, Address* address) {
	MAGIC_ASSERT(dns);
	if(!address_isLocal(address)) {
		if(g_hash_table_remove(dns->addressByIP, GUINT_TO_POINTER(address_toHostIP(address)))) {
			address_unref(address);
		}
		if(g_hash_table_remove(dns->addressByName, address_toHostName(address))) {
			address_unref(address);
		}
	}
}

Address* dns_resolveIPToAddress(DNS* dns, guint32 ip) {
	MAGIC_ASSERT(dns);
	return (Address*) g_hash_table_lookup(dns->addressByIP, &ip);
}

Address* dns_resolveNameToAddress(DNS* dns, gchar* name) {
	MAGIC_ASSERT(dns);
	return (Address*) g_hash_table_lookup(dns->addressByName, name);
}

//TODO remove this func
guint32 dns_resolveNameToIP(DNS* dns, gchar* name) {
	MAGIC_ASSERT(dns);
	Address* address = dns_resolveNameToAddress(dns, name);
	if(address) {
		return address_toNetworkIP(address);
	} else {
		return INADDR_NONE;
	}
}

//TODO remove this func
const gchar* dns_resolveIPToName(DNS* dns, guint32 ip) {
	MAGIC_ASSERT(dns);
	Address* address = dns_resolveIPToAddress(dns, ip);
	if(address) {
		return address_toHostName(address);
	} else {
		return NULL;
	}
}

DNS* dns_new() {
	DNS* dns = g_new0(DNS, 1);
	MAGIC_INIT(dns);

	dns->addressByIP = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) address_unref);
	dns->addressByName = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) address_unref);

	return dns;
}

void dns_free(DNS* dns) {
	MAGIC_ASSERT(dns);

	g_hash_table_destroy(dns->addressByIP);
	g_hash_table_destroy(dns->addressByName);

	MAGIC_CLEAR(dns);
	g_free(dns);
}
