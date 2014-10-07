/*
 * See LICENSE for licensing information
 */

#include "shd-tgen.h"

struct _TGenPeer {
    in_addr_t netIP;
    in_port_t netPort;
    gchar* hostIPStr;
    gchar* hostNameStr;

    gchar* string;

    gint refcount;
    guint magic;
};

static in_addr_t _tgenpeer_ipStrToIP(const gchar* string) {
    struct sockaddr_in sa;
    int result = inet_pton(AF_INET, string, &(sa.sin_addr));

    if(result == 1) {
        return sa.sin_addr.s_addr;
    } else {
        return (in_addr_t)htonl(INADDR_NONE);
    }
}

static gchar* _tgenpeer_ipToIPStr(in_addr_t netIP) {
    gchar* ipStr = NULL;

    if(netIP != htonl(INADDR_NONE)) {
        gchar netbuf[INET_ADDRSTRLEN+1];
        memset(netbuf, 0, INET_ADDRSTRLEN+1);

        const gchar* netresult = inet_ntop(AF_INET, &netIP, netbuf, INET_ADDRSTRLEN);

        if(netresult != NULL) {
            ipStr = g_strdup(netresult);
        }
    }

    return ipStr;
}

static in_addr_t _tgenpeer_lookupIP(const gchar* hostname) {
    in_addr_t ip = htonl(INADDR_NONE);

    struct addrinfo* info = NULL;

    /* this call does the network query */
    gint result = getaddrinfo((gchar*) hostname, NULL, NULL, &info);

    if (result == 0) {
        ip = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
    } else {
        tgen_warning("getaddrinfo(): returned %i host '%s' errno %i: %s",
                result, hostname, errno, g_strerror(errno));
    }

    freeaddrinfo(info);

    return ip;
}

static gchar* _tgenpeer_lookupName(in_addr_t networkIP) {
    gchar* name = NULL;

    struct sockaddr_in addrbuf;
    memset(&addrbuf, 0, sizeof(struct sockaddr_in));
    addrbuf.sin_addr.s_addr = networkIP;

    gchar namebuf[256];
    memset(namebuf, 0, 256);

    /* this call does the network query */
    gint result = getnameinfo((struct sockaddr*)&addrbuf, (socklen_t) sizeof(struct sockaddr_in),
            namebuf, (socklen_t) 255, NULL, 0, 0);

    if(result == 0) {
        name = g_strdup(namebuf);
    } else {
        gchar* ipStr = _tgenpeer_ipToIPStr(networkIP);
        tgen_warning("getnameinfo(): returned %i ip '%s' errno %i: %s",
                result, ipStr, errno, g_strerror(errno));
        if(ipStr) {
            g_free(ipStr);
        }
    }

    return name;
}

void tgenpeer_performLookups(TGenPeer* peer) {
    TGEN_ASSERT(peer);

    /* it is safe to perform network lookups to fill in our ip/name info */
    gboolean changed = FALSE;

    /* hostname lookup */
    if(peer->netIP && !peer->hostNameStr) {
        peer->hostNameStr = _tgenpeer_lookupName(peer->netIP);
        changed = TRUE;
    }

    /* address lookup */
    if(peer->hostNameStr && !peer->netIP) {
        in_addr_t ipTry = _tgenpeer_lookupIP(peer->hostNameStr);
        if(ipTry != htonl(INADDR_NONE)) {
            peer->netIP = ipTry;
            if(peer->hostIPStr) {
                g_free(peer->hostIPStr);
            }
            peer->hostIPStr = _tgenpeer_ipToIPStr((in_addr_t)ntohl(peer->netIP));
            changed = TRUE;
        }
    }

    /* if info changed, update our peer string when next requested */
    if(changed) {
        if(peer->string) {
            g_free(peer->string);
            peer->string = NULL;
        }
    }
}

static TGenPeer* _tgenpeer_new(const gchar* name, in_addr_t networkIP, in_port_t networkPort) {
    TGenPeer* peer = g_new0(TGenPeer, 1);
    peer->magic = TGEN_MAGIC;
    peer->refcount = 1;

    peer->netPort = networkPort;

    if(name) {
        /* the name could be a hostname, or a dots-and-decimal ip string */
        if (!g_ascii_strcasecmp(name, "localhost")) {
            peer->netIP = htonl(INADDR_LOOPBACK);
        } else if (!g_ascii_strcasecmp(name, "0.0.0.0")) {
            peer->netIP = htonl(INADDR_ANY);
        } else {
            /* check for dots-and-decimal format */
            in_addr_t ipTry = _tgenpeer_ipStrToIP(name);
            if(ipTry != htonl(INADDR_NONE)) {
                /* found valid ip address in the string */
                peer->netIP = ipTry;
            } else {
                /* not a valid ip, lets assume its a hostname */
                peer->hostNameStr = g_strdup(name);
            }
        }
    }

    if(networkIP != htonl(INADDR_NONE)) {
        peer->netIP = networkIP;
    }

    if(peer->netIP) {
        peer->hostIPStr = _tgenpeer_ipToIPStr((in_addr_t)ntohl(networkIP));
    }

    return peer;
}

TGenPeer* tgenpeer_newFromName(const gchar* name, in_port_t networkPort) {
    return _tgenpeer_new(name, htonl(INADDR_NONE), networkPort);
}

TGenPeer* tgenpeer_newFromIP(in_addr_t networkIP, in_port_t networkPort) {
    return _tgenpeer_new(NULL, networkIP, networkPort);
}

static void _tgenpeer_free(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    g_assert(peer->refcount == 0);

    if(peer->hostIPStr) {
        g_free(peer->hostIPStr);
    }

    if(peer->hostNameStr) {
        g_free(peer->hostNameStr);
    }

    if(peer->string) {
        g_free(peer->string);
    }

    peer->magic = 0;
    g_free(peer);
}

void tgenpeer_ref(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    peer->refcount++;
}

void tgenpeer_unref(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    if(--(peer->refcount) <= 0) {
        _tgenpeer_free(peer);
    }
}

in_addr_t tgenpeer_getNetworkIP(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    return peer->netIP;
}

in_port_t tgenpeer_getNetworkPort(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    return peer->netPort;
}

in_addr_t tgenpeer_getHostIP(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    return ntohl(peer->netIP);
}

in_port_t tgenpeer_getHostPort(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    return ntohs(peer->netPort);
}

const gchar* tgenpeer_getName(TGenPeer* peer) {
    TGEN_ASSERT(peer);
    return peer->hostNameStr;
}

const gchar* tgenpeer_toString(TGenPeer* peer) {
    TGEN_ASSERT(peer);

    if(!peer->string) {
        GString* stringBuffer = g_string_new(NULL);
        g_string_printf(stringBuffer, "%s:%s:%u", peer->hostNameStr, peer->hostIPStr,
                ntohs(peer->netPort));
        peer->string = g_string_free(stringBuffer, FALSE);
    }

    return peer->string;
}
