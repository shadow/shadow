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

TGenPeer* tgenpeer_new(in_addr_t networkIP, in_port_t networkPort) {
    TGenPeer* peer = g_new0(TGenPeer, 1);
    peer->magic = TGEN_MAGIC;
    peer->refcount = 1;

    peer->netIP = networkIP;
    peer->netPort = networkPort;

    gchar netbuf[INET_ADDRSTRLEN+1];
    memset(netbuf, 0, INET_ADDRSTRLEN+1);
    const gchar* netresult = inet_ntop(AF_INET, &networkIP, netbuf, INET_ADDRSTRLEN);

    if(netresult) {
        peer->hostIPStr = g_strdup(netresult);
    }

    struct sockaddr_in addrbuf;
    memset(netbuf, 0, sizeof(struct sockaddr_in));
    addrbuf.sin_addr.s_addr = networkIP;
    addrbuf.sin_port = networkPort;

    gchar hostbuf[256];
    memset(hostbuf, 0, 256);
    gint hostresult = getnameinfo((struct sockaddr*)&addrbuf, (socklen_t) sizeof(struct sockaddr_in),
            hostbuf, (socklen_t) 255, NULL, 0, 0);

    if(hostresult == 0) {
        peer->hostNameStr = g_strdup(hostbuf);
    }

    return peer;
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
