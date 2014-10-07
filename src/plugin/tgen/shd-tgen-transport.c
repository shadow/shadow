/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

typedef enum {
    PROXY_INIT, PROXY_CHOICE, PROXY_REQUEST, PROXY_RESPONSE, PROXY_SUCCESS, PROXY_ERROR
} ProxyState;

struct _TGenTransport {
    TGenTransportProtocol protocol;
    gint socketD;

    TGenTransport_notifyBytesFunc notify;
    gpointer data;
    GDestroyNotify destructData;

    TGenPeer* peer;
    gchar* string;

    TGenPeer* proxy;
    ProxyState proxyState;

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

    /* connect to another host */
    struct sockaddr_in master;
    memset(&master, 0, sizeof(master));
    master.sin_family = AF_INET;

    /* if there is a proxy, we connect there; otherwise connect to the peer */
    TGenPeer* connectee = proxy ? proxy : peer;

    /* its safe to do lookups on whoever we are directly connecting to. */
    tgenpeer_performLookups(connectee);

    master.sin_addr.s_addr = tgenpeer_getNetworkIP(connectee);
    master.sin_port = tgenpeer_getNetworkPort(connectee);

    gint result = connect(socketD, (struct sockaddr *) &master, sizeof(master));

    /* nonblocking sockets means inprogress is ok */
    if (result < 0 && errno != EINPROGRESS) {
        tgen_critical("connect(): socket %i returned %i error %i: %s",
                socketD, result, errno, g_strerror(errno));
        close(socketD);
        return NULL;
    }

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

gboolean tgentransport_wantsEvents(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->proxy && transport->proxyState != PROXY_SUCCESS
            && transport->proxyState != PROXY_ERROR) {
        return TRUE;
    }
    return FALSE;
}

static TGenEvent _tgentransport_sendSocksInit(TGenTransport* transport) {
    /*
    1 socks init client --> server
    \x05 (version 5)
    \x01 (1 supported auth method)
    \x00 (method is "no auth")
    */
    gssize bytesSent = tgentransport_write(transport, "\x05\x01\x00", 3);
    g_assert(bytesSent == 3);

    tgen_debug("sent socks init to proxy %s", tgenpeer_toString(transport->proxy));

    transport->proxyState = PROXY_CHOICE;
    return TGEN_EVENT_READ;
}

static TGenEvent _tgentransport_receiveSocksChoice(TGenTransport* transport) {
    /*
    2 socks choice client <-- server
    \x05 (version 5)
    \x00 (auth method choice - \xFF means none supported)
    */
    gchar buffer[8];
    memset(buffer, 0, 8);
    gssize bytesReceived = tgentransport_read(transport, buffer, 2);
    g_assert(bytesReceived == 2);

    if(buffer[0] == 0x05 && buffer[1] == 0x00) {
        tgen_debug("received good socks choice from proxy %s", tgenpeer_toString(transport->proxy));

        transport->proxyState = PROXY_REQUEST;
        return TGEN_EVENT_WRITE;
    } else {
        transport->proxyState = PROXY_ERROR;
        return TGEN_EVENT_NONE;
    }
}

static TGenEvent _tgentransport_sendSocksRequest(TGenTransport* transport) {
    /*
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
    */

    /* prefer name mode if we have it, and let the proxy lookup IP as needed */
    const gchar* name = tgenpeer_getName(transport->peer);
    if(name) {
        /* case 3b - domain name */
        glong nameLength = g_utf8_strlen(name, -1);
        guint8 guint8max = -1;
        if(nameLength > guint8max) {
            nameLength = (glong)guint8max;
            tgen_warning("truncated name '%s' in socks request from %i to %u bytes",
                    name, nameLength, (guint)guint8max);
        }

        in_addr_t port = tgenpeer_getNetworkPort(transport->peer);

        gchar buffer[nameLength+8];
        memset(buffer, 0, nameLength+8);

        g_memmove(&buffer[0], "\x05\x01\x00\x03", 4);
        g_memmove(&buffer[4], &nameLength, 1);
        g_memmove(&buffer[5], name, nameLength);
        g_memmove(&buffer[5+nameLength], &port, 2);

        gssize bytesSent = tgentransport_write(transport, buffer, nameLength+7);
        g_assert(bytesSent == nameLength+8);
    } else {
        /* case 3a - IPv4 */
        in_addr_t ip = tgenpeer_getNetworkIP(transport->peer);
        in_addr_t port = tgenpeer_getNetworkPort(transport->peer);

        gchar buffer[16];
        memset(buffer, 0, 16);

        g_memmove(&buffer[0], "\x05\x01\x00\x01", 4);
        g_memmove(&buffer[4], &ip, 4);
        g_memmove(&buffer[8], &port, 2);

        gssize bytesSent = tgentransport_write(transport, buffer, 10);
        g_assert(bytesSent == 10);
    }

    tgen_debug("requested connection to %s through socks proxy %s",
            tgenpeer_toString(transport->peer), tgenpeer_toString(transport->proxy));

    transport->proxyState = PROXY_RESPONSE;
    return TGEN_EVENT_READ;
}

static TGenEvent _tgentransport_receiveSocksResponse(TGenTransport* transport) {
    /*
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
     */

    gchar buffer[256];
    memset(buffer, 0, 256);
    gssize bytesReceived = tgentransport_read(transport, buffer, 256);
    g_assert(bytesReceived >= 4);

    if(buffer[0] == 0x05 && buffer[1] == 0x00 && buffer[3] == 0x01) {
        /* case 4a - IPV4 mode - get address server told us */
        g_assert(bytesReceived == 10);

        /* check if they want us to connect elsewhere */
        in_addr_t socksBindAddress = 0;
        in_port_t socksBindPort = 0;
        g_memmove(&socksBindAddress, &buffer[4], 4);
        g_memmove(&socksBindPort, &buffer[8], 2);

        /* reconnect not supported */
        if(socksBindAddress == 0 && socksBindPort == 0) {
            tgen_info("connection to %s through socks proxy %s successful",
                    tgenpeer_toString(transport->peer), tgenpeer_toString(transport->proxy));

            transport->proxyState = PROXY_SUCCESS;
            return TGEN_EVENT_DONE;
        }
    } else if (buffer[0] == 0x05 && buffer[1] == 0x00 && buffer[3] == 0x03) {
        /* case 4b - domain name mode */
        guint8 nameLength = 0;
        g_memmove(&nameLength, &buffer[4], 1);

        g_assert(bytesReceived == nameLength+7);

        gchar namebuf[nameLength+1];
        memset(namebuf, 0, nameLength);
        in_port_t socksBindPort = 0;

        g_memmove(namebuf, &buffer[5], nameLength);
        g_memmove(&socksBindPort, &buffer[5+nameLength], 2);

        /* reconnect not supported */
        if(!g_ascii_strncasecmp(namebuf, "\0", (gsize) 1) && socksBindPort == 0) {
            tgen_info("connection to %s through socks proxy %s successful",
                    tgenpeer_toString(transport->peer), tgenpeer_toString(transport->proxy));

            transport->proxyState = PROXY_SUCCESS;
            return TGEN_EVENT_DONE;
        }
    }

    transport->proxyState = PROXY_ERROR;
    return TGEN_EVENT_NONE;
}

TGenEvent tgentransport_onEvent(TGenTransport* transport, TGenEvent events) {
    TGEN_ASSERT(transport);
    if(!tgentransport_wantsEvents(transport)) {
        return TGEN_EVENT_NONE;
    }

    switch(transport->proxyState) {
    case PROXY_INIT: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksInit(transport);
        }
    }

    case PROXY_CHOICE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksChoice(transport);
        }
    }

    case PROXY_REQUEST: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksRequest(transport);
        }
    }

    case PROXY_RESPONSE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponse(transport);
        }
    }

    case PROXY_SUCCESS: {
        return TGEN_EVENT_DONE;
    }

    case PROXY_ERROR:
    default: {
        return TGEN_EVENT_NONE;
    }
    }
}
