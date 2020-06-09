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
#include <unistd.h>

#include "main/core/support/definitions.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _DNS {
    GMutex lock;

    in_addr_t ipAddressCounter;
    guint macAddressCounter;

    /* address mappings */
    GHashTable* addressByIP;
    GHashTable* addressByName;

    struct {
        int filenum;
        char* path;
        bool isStale;
    } hosts;

    MAGIC_DECLARE;
};

static gboolean _dns_isIPInRange(const in_addr_t netIP, const gchar* cidrStr) {
    utility_assert(cidrStr);

    gchar** cidrParts = g_strsplit(cidrStr, "/", 0);
    gchar* cidrIPStr = cidrParts[0];
    gint cidrBits = atoi(cidrParts[1]);
    utility_assert(cidrBits >= 0 && cidrBits <= 32);

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
        debug("ip '%s' is in range '%s' using subnet '%s' and mask '%s'",
                ipStr, cidrStr, subnetIPStr, netmaskStr);
        g_free(ipStr);
        g_free(subnetIPStr);
        g_free(netmaskStr);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _dns_isRestricted(DNS* dns, in_addr_t netIP) {
    /* http://en.wikipedia.org/wiki/Reserved_IP_addresses#Reserved_IPv4_addresses */
    if(_dns_isIPInRange(netIP, "0.0.0.0/8") ||
            _dns_isIPInRange(netIP, "10.0.0.0/8") ||
            _dns_isIPInRange(netIP, "100.64.0.0/10") ||
            _dns_isIPInRange(netIP, "127.0.0.0/8") ||
            _dns_isIPInRange(netIP, "169.254.0.0/16") ||
            _dns_isIPInRange(netIP, "172.16.0.0/12") ||
            _dns_isIPInRange(netIP, "192.0.0.0/29") ||
            _dns_isIPInRange(netIP, "192.0.2.0/24") ||
            _dns_isIPInRange(netIP, "192.88.99.0/24") ||
            _dns_isIPInRange(netIP, "192.168.0.0/16") ||
            _dns_isIPInRange(netIP, "198.18.0.0/15") ||
            _dns_isIPInRange(netIP, "198.51.100.0/24") ||
            _dns_isIPInRange(netIP, "203.0.113.0/24") ||
            _dns_isIPInRange(netIP, "224.0.0.0/4") ||
            _dns_isIPInRange(netIP, "240.0.0.0/4") ||
            _dns_isIPInRange(netIP, "255.255.255.255/32")) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _dns_isIPUnique(DNS* dns, in_addr_t ip) {
    gboolean exists = g_hash_table_lookup_extended(dns->addressByIP, GUINT_TO_POINTER(ip), NULL, NULL);
    return exists ? FALSE : TRUE;
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
    utility_assert(name);

    g_mutex_lock(&dns->lock);

    in_addr_t ip = 0;
    guint mac = ++dns->macAddressCounter;
    gboolean isLocal = FALSE;

    /* if requestedIP is NULL, we should generate one ourselves */
    if(requestedIP) {
        ip = address_stringToIP(requestedIP);
        /* restricted is OK if this is a localhost address, otherwise it must be unique */
        if(ip == address_stringToIP("127.0.0.1")) {
            isLocal = TRUE;
        } else if(_dns_isRestricted(dns, ip) || !_dns_isIPUnique(dns, ip)) {
            ip = _dns_generateIP(dns);
        }
    } else {
        ip = _dns_generateIP(dns);
    }

    Address* address = address_new(id, mac, (guint32) ip, name, isLocal);

    /* store the ip/name mappings */
    if(!isLocal) {
        g_hash_table_replace(dns->addressByIP, GUINT_TO_POINTER(address_toNetworkIP(address)), address);
        address_ref(address);
        g_hash_table_replace(dns->addressByName, address_toHostName(address), address);
        address_ref(address);
    }

    /* Any existing hosts file needs to be (lazily) updated. */
    dns->hosts.isStale;

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
        dns->hosts.isStale;

        g_mutex_unlock(&dns->lock);
    }
}

Address* dns_resolveIPToAddress(DNS* dns, in_addr_t ip) {
    MAGIC_ASSERT(dns);
    Address* result = g_hash_table_lookup(dns->addressByIP, GUINT_TO_POINTER(ip));
    if(!result) {
        gchar* ipStr = address_ipToNewString(ip);
        info("address for '%s' does not yet exist", ipStr);
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

static void _dns_cleanupHostsFile(DNS* dns) {
    MAGIC_ASSERT(dns);

    if(dns->hosts.filenum > 0) {
        close(dns->hosts.filenum);
        dns->hosts.filenum = 0;
    }

    if(dns->hosts.path) {
        free(dns->hosts.path);
        dns->hosts.path = NULL;
    }
}

static char* _dns_getHostsPath(DNS* dns) {
    char* abspath = NULL;
    if (asprintf(&abspath, "/tmp/shadow-%i-hosts-XXXXXX", (int)getpid()) < 0) {
        error("asprintf could not allocate string for hosts file");
        abort();
    }
    return abspath;
}

static void _dns_writeHostLine(gpointer key, gpointer value, gpointer data) {
    gchar* name = key;
    Address* address = value;
    GString* buf = data;
    g_string_append_printf(buf, "%s %s\n", address_toHostIPString(address), name);
}

static bool _dns_writeNewHostsFile(DNS* dns) {
    MAGIC_ASSERT(dns);
    utility_assert(!dns->hosts.path);

    dns->hosts.path = _dns_getHostsPath(dns);
    dns->hosts.filenum = mkstemp(dns->hosts.path);
    if(dns->hosts.filenum < 0) {
        warning("Unable create temp hosts file, mkstemp() error %i: %s", errno, strerror(errno));
        return false;
    }

    GString* buf = g_string_new("127.0.0.1 localhost\n");
    g_hash_table_foreach(dns->addressByName, _dns_writeHostLine, buf);

    debug("Hosts file string buffer is %zu bytes.", buf->len);

    size_t amt = 0;
    while(amt < buf->len) {
        ssize_t ret = write(dns->hosts.filenum, &buf->str[amt], buf->len-amt);
        if(ret < 0 && errno != EAGAIN) {
            warning("Unable to write to temp hosts file, write() error %i: %s", errno, strerror(errno));
            g_string_free(buf, TRUE);
            return false;
        } else if(ret >= 0) {
            amt += (size_t)ret;
        }
    }

    message("Wrote new hosts file of size %zu bytes at path '%s'", amt, dns->hosts.path);
    dns->hosts.isStale = false;
    g_string_free(buf, TRUE);
    return true;
}

gchar* dns_getHostsFilePath(DNS* dns) {
    MAGIC_ASSERT(dns);

    char* path = NULL;

    g_mutex_lock(&dns->lock);

    if(dns->hosts.isStale || !dns->hosts.path) {
        _dns_cleanupHostsFile(dns);
        if(!_dns_writeNewHostsFile(dns)) {
            warning("Unable to create hosts file; expect networking errors.");
            _dns_cleanupHostsFile(dns);
        }
    }

    if(dns->hosts.path) {
        path = strdup(dns->hosts.path);
    }

    g_mutex_unlock(&dns->lock);

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

    return dns;
}

void dns_free(DNS* dns) {
    MAGIC_ASSERT(dns);

    _dns_cleanupHostsFile(dns);

    g_hash_table_destroy(dns->addressByIP);
    g_hash_table_destroy(dns->addressByName);

    g_mutex_clear(&(dns->lock));

    MAGIC_CLEAR(dns);
    g_free(dns);
}
