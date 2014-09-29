/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGenTransport {
    TGenTransportProtocol protocol;
    gint socketD;

    TGenTransport_notifyBytesFunc notify;
    gpointer data;
    GDestroyNotify destructData;

    TGenPeer* peer;
    TGenPeer* proxy;
    gchar* string;

    gint refcount;
    guint magic;
};

static TGenTransport* _tgentransport_newHelper(gint socketD, TGenPeer* proxy, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    TGenTransport* transport = g_new0(TGenTransport, 1);
    transport->magic = TGEN_MAGIC;
    transport->refcount = 1;

    transport->socketD = socketD;
    transport->protocol = TGEN_PROTOCOL_TCP;

    if(peer) {
        transport->peer = peer;
        tgenpeer_ref(peer);
    }
    if(proxy) {
        transport->proxy = proxy;
        tgenpeer_ref(proxy);
    }

    transport->notify = notify;
    transport->data = data;
    transport->destructData = destructData;

    return transport;
}

TGenTransport* tgentransport_newActive(TGenPeer* proxy, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    /* create the socket and get a socket descriptor */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (socketD < 0) {
        tgen_critical("socket(): returned %i error %i: %s",
                socketD, errno, g_strerror(errno));
        return NULL;
    }

    /* connect to the given peer */
    struct sockaddr_in master;
    memset(&master, 0, sizeof(master));
    master.sin_family = AF_INET;
    master.sin_addr.s_addr = tgenpeer_getNetworkIP(peer);
    master.sin_port = tgenpeer_getNetworkPort(peer);

    gint result = connect(socketD, (struct sockaddr *) &master, sizeof(master));

    /* nonblocking sockets means inprogress is ok */
    if (result < 0 && errno != EINPROGRESS) {
        tgen_critical("connect(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

    // TODO handle socks connection if proxy exists
    /*
    --------------------------------
    1 socks init client --> server
    \x05 (version 5)
    \x01 (1 supported auth method)
    \x00 (method is "no auth")
    --------------------------------

    2 socks choice client <-- server
    \x05 (version 5)
    \x00 (auth method choice - \xFF means none supported)
    --------------------------------

    3 socks request client --> server
    \x05 (version 5)
    \x01 (tcp stream)
    \x00 (reserved)

    the client asks the server to connect to a remote

    3a ip address client --> server
    \x01 (ipv4)
    in_addr_t (4 bytes)
    in_port_t (2 bytes)

    3b hostname client --> server
    \x03 (domain name)
    \x__ (1 byte name len)
    (name)
    in_port_t (2 bytes)
    --------------------------------

    4 socks response client <-- server
    \x05 (version 5)
    \x00 (request granted)
    \x00 (reserved)

    the server can tell us that we need to reconnect elsewhere

    4a ip address client <-- server
    \x01 (ipv4)
    in_addr_t (4 bytes)
    in_port_t (2 bytes)

    4b hostname client <-- server
    \x03 (domain name)
    \x__ (1 byte name len)
    (name)
    in_port_t (2 bytes)
    --------------------------------
     */

    return _tgentransport_newHelper(socketD, proxy, peer, notify, data, destructData);
}

TGenTransport* tgentransport_newPassive(gint socketD, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    return _tgentransport_newHelper(socketD, NULL, peer, notify, data, destructData);
}

static void _tgentransport_free(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(transport->socketD > 0) {
        close(transport->socketD);
    }

    if(transport->string) {
        g_free(transport->string);
    }

    if(transport->peer) {
        tgenpeer_unref(transport->peer);
    }

    if(transport->proxy) {
        tgenpeer_unref(transport->proxy);
    }

    if(transport->destructData && transport->data) {
        transport->destructData(transport->data);
    }

    transport->magic = 0;
    g_free(transport);
}

void tgentransport_ref(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    transport->refcount++;
}

void tgentransport_unref(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    transport->refcount--;
    if(transport->refcount == 0) {
        _tgentransport_free(transport);
    }
}

gssize tgentransport_write(TGenTransport* transport, gpointer buffer, gsize length) {
    TGEN_ASSERT(transport);

    gssize bytes = write(transport->socketD, buffer, length);

    if(bytes > 0 && transport->notify) {
        transport->notify(transport->data, 0, bytes);
    }

    return bytes;
}

gssize tgentransport_read(TGenTransport* transport, gpointer buffer, gsize length) {
    TGEN_ASSERT(transport);

    gssize bytes = read(transport->socketD, buffer, length);

    if(bytes > 0 && transport->notify) {
        transport->notify(transport->data, bytes, 0);
    }

    return bytes;
}

gint tgentransport_getDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->socketD;
}

const gchar* tgentransport_toString(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(!transport->string) {
        GString* buffer = g_string_new(NULL);

        if(transport->proxy && transport->peer) {
            g_string_printf(buffer, "(TCP-%i-%s-%s)", transport->socketD,
                    tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->peer));
        } else if(transport->peer) {
            g_string_printf(buffer, "(TCP-%i-%s)", transport->socketD,
                    tgenpeer_toString(transport->peer));
        } else {
            g_string_printf(buffer, "(TCP-%i)", transport->socketD);
        }

        transport->string = g_string_free(buffer, FALSE);
    }

    return transport->string;
}
