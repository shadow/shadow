/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/*
 * host order INADDR_LOOPBACK: 2130706433, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 * network order INADDR_LOOPBACK: 16777343, INADDR_ANY: 0, INADDR_NONE: 4294967295, INADDR_BROADCAST: 4294967295
 */

/* IP must be first so we can cast an Address to an in_addr_t */
#include <arpa/inet.h>
#include <glib.h>
#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>

#include "main/routing/address.h"

#include "main/utility/utility.h"

struct _Address {
    /* the IP in network-order */
    guint32 ip;

    /* globally unique mac address */
    guint mac;

    /* the host-order IP in dots-and-decimals format */
    gchar* ipString;

    /* the hostname */
    gchar* name;

    gchar* idString;

    gint referenceCount;

    gboolean isLocal;

    HostId hostID;
    MAGIC_DECLARE;
};

Address* address_new(HostId hostID, guint mac, guint32 ip, const gchar* name, gboolean isLocal) {
    Address* address = g_new0(Address, 1);
    MAGIC_INIT(address);

    address->hostID = hostID;
    address->mac = mac;
    address->ip = ip;
    address->ipString = address_ipToNewString((in_addr_t)ip);
    address->isLocal = isLocal;
    address->name = g_strdup(name);
    address->referenceCount = 1;

    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "%s-%s (%s,mac=%i)", address->name, address->ipString,
            address->isLocal ? "lo" : "eth", address->mac);
    address->idString = g_string_free(stringBuffer, FALSE);

    return address;
}

static void _address_free(Address* address) {
    MAGIC_ASSERT(address);

    g_free(address->ipString);
    g_free(address->name);
    g_free(address->idString);

    MAGIC_CLEAR(address);
    g_free(address);
}

HostId address_getID(const Address* address) {
    MAGIC_ASSERT(address);
    return address->hostID;
}

void address_ref(Address* address) {
    MAGIC_ASSERT(address);
    address->referenceCount++;
}

void address_unref(Address* address) {
    MAGIC_ASSERT(address);
    address->referenceCount--;
    if(address->referenceCount <= 0) {
        _address_free(address);
    }
}

gboolean address_isLocal(const Address* address) {
    MAGIC_ASSERT(address);
    return address->isLocal;
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

guint32 address_toHostIP(const Address* address) {
    MAGIC_ASSERT(address);
    return ntohl(address->ip);
}

const gchar* address_toHostIPString(const Address* address) {
    MAGIC_ASSERT(address);
    return address->ipString;
}

guint32 address_toNetworkIP(const Address* address) {
    MAGIC_ASSERT(address);
    return address->ip;
}

const gchar* address_toHostName(const Address* address) {
    MAGIC_ASSERT(address);
    return address->name;
}

const gchar* address_toString(const Address* address) {
    MAGIC_ASSERT(address);
    return address->idString;
}

// Address must be in network byte order.
gchar* address_ipToNewString(in_addr_t ip) {
    gchar* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN + 1);
    struct in_addr addr = {.s_addr = ip};
    const gchar* ipString = inet_ntop(AF_INET, &addr, ipStringBuffer, INET6_ADDRSTRLEN);
    GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
    g_free(ipStringBuffer);
    return g_string_free(result, FALSE);
}

// Returned address will be in network byte order.
in_addr_t address_stringToIP(const gchar* ipString) {
    struct in_addr inaddr;
    if(1 == inet_pton(AF_INET, ipString, &inaddr)) {
        return inaddr.s_addr;
    } else {
        return INADDR_NONE;
    }
}
