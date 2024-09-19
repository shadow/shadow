/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/core/definitions.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/utility.h"

struct _DNS {
    GMutex lock;

    /* address in network byte order */
    in_addr_t ipAddressCounter;
    guint macAddressCounter;

    /* address mappings */
    GHashTable* addressByIP;
    GHashTable* addressByName;

    int hosts_file_fd;

    MAGIC_DECLARE;
};

/* Address must be in network byte order. */
static gboolean _dns_isIPInRange(const in_addr_t netIP, const gchar* cidrStr) {
    utility_debugAssert(cidrStr);

    gchar** cidrParts = g_strsplit(cidrStr, "/", 0);
    gchar* cidrIPStr = cidrParts[0];
    gint cidrBits = atoi(cidrParts[1]);
    utility_debugAssert(cidrBits >= 0 && cidrBits <= 32);

    /* first create the mask in host order */
    in_addr_t netmask = 0;
    for(gint i = 0; i < 32; i++) {
        /* move one so LSB is 0 */
        netmask = netmask << 1;
        if(cidrBits > i) {
            /* flip the LSB */
            netmask++;
        }
    }

    /* flip to network order */
    netmask = htonl(netmask);

    /* get the subnet ip in network order */
    in_addr_t subnetIP = address_stringToIP(cidrIPStr);

    g_strfreev(cidrParts);

    /* all non-subnet bits should be flipped */
    if((netIP & netmask) == (subnetIP & netmask)) {
        gchar* ipStr = address_ipToNewString(netIP);
        gchar* subnetIPStr = address_ipToNewString(subnetIP);
        gchar* netmaskStr = address_ipToNewString(netmask);
        trace("ip '%s' is in range '%s' using subnet '%s' and mask '%s'",
                ipStr, cidrStr, subnetIPStr, netmaskStr);
        g_free(ipStr);
        g_free(subnetIPStr);
        g_free(netmaskStr);
        return TRUE;
    } else {
        return FALSE;
    }
}

/* Address must be in network byte order. */
static gboolean _dns_isIPUnique(DNS* dns, in_addr_t ip) {
    gboolean exists = g_hash_table_lookup_extended(dns->addressByIP, GUINT_TO_POINTER(ip), NULL, NULL);
    return exists ? FALSE : TRUE;
}

/* Address must be in network byte order. */
Address* dns_register(DNS* dns, HostId id, const gchar* name, in_addr_t requestedIP) {
    MAGIC_ASSERT(dns);
    utility_debugAssert(name);

    g_mutex_lock(&dns->lock);

    gboolean isLocal = FALSE;

    /* non-unique are OK only if this is a localhost address */
    /* TODO: Support other localhost addresses; e.g. 127.0.0.2? */
    if (requestedIP == address_stringToIP("127.0.0.1")) {
        isLocal = TRUE;
    } else if (!_dns_isIPUnique(dns, requestedIP)) {
        gchar* ipStr = address_ipToNewString(requestedIP);
        warning("Non-unique IP assignment: %s", ipStr);
        g_free(ipStr);
        g_mutex_unlock(&dns->lock);
        return NULL;
    }

    guint mac = ++dns->macAddressCounter;
    Address* address = address_new(id, mac, (guint32)requestedIP, name, isLocal);

    /* store the ip/name mappings */
    if (!isLocal) {
        g_hash_table_replace(
            dns->addressByIP, GUINT_TO_POINTER(address_toNetworkIP(address)), address);
        address_ref(address);
        /* cast the const pointer to non-const */
        g_hash_table_replace(dns->addressByName, (gchar*)address_toHostName(address), address);
        address_ref(address);
    }

    /* Any existing hosts file needs to be (lazily) updated. */
    if (dns->hosts_file_fd >= 0) {
        close(dns->hosts_file_fd);
        dns->hosts_file_fd = -1;
    }

    g_mutex_unlock(&dns->lock);

    return address;
}

void dns_deregister(DNS* dns, Address* address) {
    MAGIC_ASSERT(dns);
    if(!address_isLocal(address)) {
        g_mutex_lock(&dns->lock);

        /* these remove functions will call address_unref as necessary */
        g_hash_table_remove(dns->addressByIP, GUINT_TO_POINTER(address_toNetworkIP(address)));
        g_hash_table_remove(dns->addressByName, address_toHostName(address));

        /* Any existing hosts file needs to be (lazily) updated. */
        if (dns->hosts_file_fd >= 0) {
            close(dns->hosts_file_fd);
            dns->hosts_file_fd = -1;
        }

        g_mutex_unlock(&dns->lock);
    }
}

/* Address must be in network byte order. */
Address* dns_resolveIPToAddress(DNS* dns, in_addr_t ip) {
    MAGIC_ASSERT(dns);
    Address* result = g_hash_table_lookup(dns->addressByIP, GUINT_TO_POINTER(ip));
    if(!result) {
        gchar* ipStr = address_ipToNewString(ip);
        debug("address for '%s' does not yet exist", ipStr);
        g_free(ipStr);
    }
    return result;
}

Address* dns_resolveNameToAddress(DNS* dns, const gchar* name) {
    MAGIC_ASSERT(dns);
    Address* result = g_hash_table_lookup(dns->addressByName, name);
    if(!result) {
        warning("unable to find address from name '%s'", name);
    }
    return result;
}

static void _dns_writeHostLine(gpointer key, gpointer value, gpointer data) {
    const gchar* name = key;
    const Address* address = value;
    GString* buf = data;
    g_string_append_printf(buf, "%s %s\n", address_toHostIPString(address), name);
}

static bool _dns_writeNewHostsFile(DNS* dns) {
    MAGIC_ASSERT(dns);
    utility_debugAssert(dns->hosts_file_fd < 0);

    dns->hosts_file_fd = memfd_create("shadow hosts file", MFD_CLOEXEC);
    if (dns->hosts_file_fd < 0) {
        warning(
            "Unable create temp hosts file, memfd_create() error %i: %s", errno, strerror(errno));
        return false;
    }

    GString* buf = g_string_new("127.0.0.1 localhost\n");
    g_hash_table_foreach(dns->addressByName, _dns_writeHostLine, buf);

    trace("Hosts file string buffer is %zu bytes.", buf->len);

    size_t amt = 0;
    while(amt < buf->len) {
        ssize_t ret = write(dns->hosts_file_fd, &buf->str[amt], buf->len-amt);
        if(ret < 0 && errno != EAGAIN) {
            warning("Unable to write to temp hosts file, write() error %i: %s", errno, strerror(errno));
            g_string_free(buf, TRUE);
            close(dns->hosts_file_fd);
            dns->hosts_file_fd = -1;
            return false;
        } else if(ret >= 0) {
            amt += (size_t)ret;
        }
    }

    g_string_free(buf, TRUE);
    return true;
}

gchar* dns_getHostsFilePath(DNS* dns) {
    MAGIC_ASSERT(dns);

    g_mutex_lock(&dns->lock);

    if(dns->hosts_file_fd < 0) {
        if(!_dns_writeNewHostsFile(dns)) {
            warning("Unable to create hosts file; expect networking errors.");
            return NULL;
        }
    }

    int fd = dns->hosts_file_fd;

    g_mutex_unlock(&dns->lock);

    char* path = NULL;
    if (asprintf(&path, "/proc/%ld/fd/%i", (long)getpid(), fd) < 0) {
        utility_panic("asprintf could not allocate string for hosts file path");
        abort();
    }

    // TODO: there's a race condition here where another thread could close and
    // invalidate this hosts file before the calling code can use this path
    return path;
}

DNS* dns_new() {
    DNS* dns = g_new0(DNS, 1);
    MAGIC_INIT(dns);

    g_mutex_init(&(dns->lock));

    dns->addressByIP = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) address_unref);
    dns->addressByName = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) address_unref);

    /* 11.0.0.0 -- 100.0.0.0 is the longest available unrestricted range */
    dns->ipAddressCounter = ntohl(address_stringToIP("11.0.0.0"));

    dns->hosts_file_fd = -1;

    return dns;
}

void dns_free(DNS* dns) {
    MAGIC_ASSERT(dns);

    if (dns->hosts_file_fd >= 0) {
        close(dns->hosts_file_fd);
        dns->hosts_file_fd = -1;
    }

    g_hash_table_destroy(dns->addressByIP);
    g_hash_table_destroy(dns->addressByName);

    g_mutex_clear(&(dns->lock));

    MAGIC_CLEAR(dns);
    g_free(dns);
}
