/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

typedef enum {
    TGEN_XPORT_CONNECT,
    TGEN_XPORT_PROXY_INIT, TGEN_XPORT_PROXY_CHOICE,
    TGEN_XPORT_PROXY_REQUEST, TGEN_XPORT_PROXY_RESPONSE,
    TGEN_XPORT_SUCCESS, TGEN_XPORT_ERROR
} TGenTransportState;

typedef enum {
    TGEN_XPORT_ERR_NONE, TGEN_XPORT_ERR_PROXY_CHOICE,
    TGEN_XPORT_ERR_PROXY_RECONN, TGEN_XPORT_ERR_PROXY_ADDR,
    TGEN_XPORT_ERR_PROXY_VERSION, TGEN_XPORT_ERR_PROXY_STATUS,
    TGEN_XPORT_ERR_WRITE, TGEN_XPORT_ERR_READ, TGEN_XPORT_ERR_MISC
} TGenTransportError;

struct _TGenTransport {
    TGenTransportState state;
    TGenTransportError error;
    gchar* string;

    TGenTransportProtocol protocol;
    gint socketD;

    TGenTransport_notifyBytesFunc notify;
    gpointer data;
    GDestroyNotify destructData;

    /* our local socket, our side of the transport */
    TGenPeer* local;
    /* non-null if we need to connect through a proxy */
    TGenPeer* proxy;
    /* the remote side of the transport */
    TGenPeer* remote;

    /* track timings for time reporting */
    struct {
        gint64 start;
        gint64 socketCreate;
        gint64 socketConnect;
        gint64 proxyInit;
        gint64 proxyChoice;
        gint64 proxyRequest;
        gint64 proxyResponse;
    } time;

    gint refcount;
    guint magic;
};

static const gchar* _tgentransport_protocolToString(TGenTransport* transport) {
    switch(transport->protocol) {
        case TGEN_PROTOCOL_TCP: {
            return "TCP";
        }
        case TGEN_PROTOCOL_UDP: {
            return "UDP";
        }
        case TGEN_PROTOCOL_PIPE: {
            return "PIPE";
        }
        case TGEN_PROTOCOL_SOCKETPAIR: {
            return "SOCKETPAIR";
        }
        case TGEN_PROTOCOL_NONE:
        default: {
            return "NONE";
        }
    }
}

static const gchar* _tgentransport_stateToString(TGenTransportState state) {
    switch(state) {
        case TGEN_XPORT_CONNECT: {
            return "CONNECT";
        }
        case TGEN_XPORT_PROXY_INIT: {
            return "INIT";
        }
        case TGEN_XPORT_PROXY_CHOICE: {
            return "CHOICE";
        }
        case TGEN_XPORT_PROXY_REQUEST: {
            return "REQUEST";
        }
        case TGEN_XPORT_PROXY_RESPONSE: {
            return "RESPONSE";
        }
        case TGEN_XPORT_SUCCESS: {
            return "SUCCESS";
        }
        case TGEN_XPORT_ERROR:
        default: {
            return "ERROR";
        }
    }
}

static const gchar* _tgentransport_errorToString(TGenTransportError error) {
    switch(error) {
        case TGEN_XPORT_ERR_NONE: {
            return "NONE";
        }
        case TGEN_XPORT_ERR_PROXY_CHOICE: {
            return "CHOICE";
        }
        case TGEN_XPORT_ERR_PROXY_RECONN: {
            return "RECONN";
        }
        case TGEN_XPORT_ERR_PROXY_ADDR: {
            return "ADDR";
        }
        case TGEN_XPORT_ERR_PROXY_VERSION: {
            return "VERSION";
        }
        case TGEN_XPORT_ERR_PROXY_STATUS: {
            return "STATUS";
        }
        case TGEN_XPORT_ERR_WRITE: {
            return "WRITE";
        }
        case TGEN_XPORT_ERR_READ: {
            return "READ";
        }
        case TGEN_XPORT_ERR_MISC:
        default: {
            return "MISC";
        }
    }
}

const gchar* tgentransport_toString(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(!transport->string) {
        const gchar* protocolStr = _tgentransport_protocolToString(transport);
        const gchar* stateStr = _tgentransport_stateToString(transport->state);
        const gchar* errorStr = _tgentransport_errorToString(transport->error);

        /* the following may be NULL, and these toString methods will handle it */
        const gchar* localStr = tgenpeer_toString(transport->local);
        const gchar* proxyStr = tgenpeer_toString(transport->proxy);
        const gchar* remoteStr = tgenpeer_toString(transport->remote);

        GString* buffer = g_string_new(NULL);

        g_string_printf(buffer, "%s,%i,%s,%s,%s,state=%s,error=%s", protocolStr, transport->socketD,
                localStr, proxyStr, remoteStr, stateStr, errorStr);

        transport->string = g_string_free(buffer, FALSE);
    }

    return transport->string;
}

static void _tgentransport_resetString(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->string) {
        g_free(transport->string);
        transport->string = NULL;
    }
}

static void _tgentransport_changeState(TGenTransport* transport, TGenTransportState state) {
    TGEN_ASSERT(transport);
    tgen_info("transport %s moving from state %s to state %s", tgentransport_toString(transport),
            _tgentransport_stateToString(transport->state), _tgentransport_stateToString(state));
    transport->state = state;
    _tgentransport_resetString(transport);
}

static void _tgentransport_changeError(TGenTransport* transport, TGenTransportError error) {
    TGEN_ASSERT(transport);
    tgen_info("transport %s moving from error %s to error %s", tgentransport_toString(transport),
            _tgentransport_errorToString(transport->error), _tgentransport_errorToString(error));
    transport->error = error;
    _tgentransport_resetString(transport);
}

static TGenTransport* _tgentransport_newHelper(gint socketD, gint64 startedTime, gint64 createdTime,
        TGenPeer* proxy, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    TGenTransport* transport = g_new0(TGenTransport, 1);
    transport->magic = TGEN_MAGIC;
    transport->refcount = 1;

    transport->socketD = socketD;
    transport->protocol = TGEN_PROTOCOL_TCP;

    if(peer) {
        transport->remote = peer;
        tgenpeer_ref(peer);
    }
    if(proxy) {
        transport->proxy = proxy;
        tgenpeer_ref(proxy);
    }

    struct sockaddr_in addrBuf;
    memset(&addrBuf, 0, sizeof(struct sockaddr_in));
    socklen_t addrBufLen = (socklen_t)sizeof(struct sockaddr_in);
    if(getsockname(socketD, (struct sockaddr*) &addrBuf, &addrBufLen) == 0) {
        transport->local = tgenpeer_newFromIP(addrBuf.sin_addr.s_addr, addrBuf.sin_port);
    }

    transport->notify = notify;
    transport->data = data;
    transport->destructData = destructData;

    transport->time.start = startedTime;
    transport->time.socketCreate = createdTime;

    return transport;
}

TGenTransport* tgentransport_newActive(TGenPeer* proxy, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    gint64 started = g_get_monotonic_time();

    /* create the socket and get a socket descriptor */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    gint64 created = g_get_monotonic_time();

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

    return _tgentransport_newHelper(socketD, started, created, proxy, peer, notify, data, destructData);
}

TGenTransport* tgentransport_newPassive(gint socketD, TGenPeer* peer,
        TGenTransport_notifyBytesFunc notify, gpointer data, GDestroyNotify destructData) {
    return _tgentransport_newHelper(socketD, 0, 0, NULL, peer, notify, data, destructData);
}

static void _tgentransport_free(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(transport->socketD > 0) {
        close(transport->socketD);
    }

    if(transport->string) {
        g_free(transport->string);
    }

    if(transport->remote) {
        tgenpeer_unref(transport->remote);
    }

    if(transport->proxy) {
        tgenpeer_unref(transport->proxy);
    }

    if(transport->local) {
        tgenpeer_unref(transport->local);
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

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_WRITE);
    }
    if(bytes > 0 && transport->notify) {
        transport->notify(transport->data, 0, (gsize)bytes);
    }

    return bytes;
}

gssize tgentransport_read(TGenTransport* transport, gpointer buffer, gsize length) {
    TGEN_ASSERT(transport);

    gssize bytes = read(transport->socketD, buffer, length);

    if(bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_READ);
    }
    if(bytes > 0 && transport->notify) {
        transport->notify(transport->data, (gsize)bytes, 0);
    }

    return bytes;
}

gint tgentransport_getDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->socketD;
}

gchar* tgentransport_getTimeStatusReport(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    gint64 create = (transport->time.socketCreate > 0 && transport->time.start > 0) ?
            (transport->time.socketCreate - transport->time.start) / 1000 : 0;
    gint64 connect = (transport->time.socketConnect > 0 && transport->time.start > 0) ?
            (transport->time.socketConnect - transport->time.start) / 1000 : 0;
    gint64 init = (transport->time.proxyInit > 0 && transport->time.start > 0) ?
            (transport->time.proxyInit - transport->time.start) / 1000 : 0;
    gint64 choice = (transport->time.proxyChoice > 0 && transport->time.start > 0) ?
            (transport->time.proxyChoice - transport->time.start) / 1000 : 0;
    gint64 request = (transport->time.proxyRequest > 0 && transport->time.start > 0) ?
            (transport->time.proxyRequest - transport->time.start) / 1000 : 0;
    gint64 response = (transport->time.proxyResponse > 0 && transport->time.start > 0) ?
            (transport->time.proxyResponse - transport->time.start) / 1000 : 0;

    GString* buffer = g_string_new(NULL);

    /* print the times in milliseconds */
    g_string_printf(buffer,
            "msecs-to-socket-create=%"G_GINT64_FORMAT" msecs-to-socket-connect=%"G_GINT64_FORMAT" "
            "msecs-to-proxy-init=%"G_GINT64_FORMAT" msecs-to-proxy-choice=%"G_GINT64_FORMAT" "
            "msecs-to-proxy-request=%"G_GINT64_FORMAT" msecs-to-proxy-response=%"G_GINT64_FORMAT,
            create, connect, init, choice, request, response);

    return g_string_free(buffer, FALSE);
}

gboolean tgentransport_wantsEvents(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    if(transport->state != TGEN_XPORT_SUCCESS && transport->state != TGEN_XPORT_ERROR) {
        return TRUE;
    }
    return FALSE;
}

static TGenEvent _tgentransport_sendSocksInit(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    /*
    1 socks init client --> server
    \x05 (version 5)
    \x01 (1 supported auth method)
    \x00 (method is "no auth")
    */
    gssize bytesSent = tgentransport_write(transport, "\x05\x01\x00", 3);
    g_assert(bytesSent == 3);

    transport->time.proxyInit = g_get_monotonic_time();
    tgen_debug("sent socks init to proxy %s", tgenpeer_toString(transport->proxy));

    _tgentransport_changeState(transport, TGEN_XPORT_PROXY_CHOICE);
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
    transport->time.proxyChoice = g_get_monotonic_time();

    if(buffer[0] == 0x05 && buffer[1] == 0x00) {
        tgen_debug("socks choice supported by proxy %s", tgenpeer_toString(transport->proxy));

        _tgentransport_changeState(transport, TGEN_XPORT_PROXY_REQUEST);
        return TGEN_EVENT_WRITE;
    } else {
        tgen_debug("socks choice unsupported by proxy %s", tgenpeer_toString(transport->proxy));

        _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
        _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_CHOICE);
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
    const gchar* name = tgenpeer_getName(transport->remote);
    if(name && g_str_has_suffix(name, ".onion")) { // FIXME remove suffix matching to have proxy do lookup for us
        /* case 3b - domain name */
        glong nameLength = g_utf8_strlen(name, -1);
        guint8 guint8max = -1;
        if(nameLength > guint8max) {
            nameLength = (glong)guint8max;
            tgen_warning("truncated name '%s' in socks request from %i to %u bytes",
                    name, nameLength, (guint)guint8max);
        }

        in_addr_t port = tgenpeer_getNetworkPort(transport->remote);

        gchar buffer[nameLength+8];
        memset(buffer, 0, nameLength+8);

        g_memmove(&buffer[0], "\x05\x01\x00\x03", 4);
        g_memmove(&buffer[4], &nameLength, 1);
        g_memmove(&buffer[5], name, nameLength);
        g_memmove(&buffer[5+nameLength], &port, 2);

        gssize bytesSent = tgentransport_write(transport, buffer, nameLength+7);
        g_assert(bytesSent == nameLength+7);
    } else {
        tgenpeer_performLookups(transport->remote); // FIXME remove this to have proxy do lookup for us
        /* case 3a - IPv4 */
        in_addr_t ip = tgenpeer_getNetworkIP(transport->remote);
        in_addr_t port = tgenpeer_getNetworkPort(transport->remote);

        gchar buffer[16];
        memset(buffer, 0, 16);

        g_memmove(&buffer[0], "\x05\x01\x00\x01", 4);
        g_memmove(&buffer[4], &ip, 4);
        g_memmove(&buffer[8], &port, 2);

        gssize bytesSent = tgentransport_write(transport, buffer, 10);
        g_assert(bytesSent == 10);
    }

    transport->time.proxyRequest = g_get_monotonic_time();
    tgen_debug("requested connection from %s through socks proxy %s to remote %s",
            tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

    _tgentransport_changeState(transport, TGEN_XPORT_PROXY_RESPONSE);
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
    transport->time.proxyResponse = g_get_monotonic_time();

    if(buffer[0] == 0x05 && buffer[1] == 0x00) {
        if(buffer[3] == 0x01) {
            /* case 4a - IPV4 mode - get address server told us */
            g_assert(bytesReceived == 10);

            /* check if they want us to connect elsewhere */
            in_addr_t socksBindAddress = 0;
            in_port_t socksBindPort = 0;
            g_memmove(&socksBindAddress, &buffer[4], 4);
            g_memmove(&socksBindPort, &buffer[8], 2);

            /* reconnect not supported */
            if(socksBindAddress == 0 && socksBindPort == 0) {
                tgen_info("connection from %s through socks proxy %s to %s successful",
                        tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

                _tgentransport_changeState(transport, TGEN_XPORT_SUCCESS);
                return TGEN_EVENT_DONE;
            } else {
                _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_RECONN);
                tgen_warning("connection from %s through socks proxy %s to %s failed: "
                        "proxy requested unsupported reconnection to %i:u",
                        tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                        (gint)socksBindAddress, (guint)ntohs(socksBindPort));
            }
        } else if (buffer[3] == 0x03) {
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
                tgen_info("connection from %s through socks proxy %s to %s successful",
                        tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote));

                _tgentransport_changeState(transport, TGEN_XPORT_SUCCESS);
                return TGEN_EVENT_DONE;
            } else {
                _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_RECONN);
                tgen_warning("connection from %s through socks proxy %s to %s failed: "
                        "proxy requested unsupported reconnection to %s:u",
                        tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                        namebuf, (guint)ntohs(socksBindPort));
            }
        } else {
            _tgentransport_changeError(transport, TGEN_XPORT_ERR_PROXY_ADDR);
            tgen_warning("connection from %s through socks proxy %s to %s failed: unsupported address type %i",
                    tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                    (gint)buffer[3]);
        }
    } else {
        _tgentransport_changeError(transport, (buffer[0] != 0x05) ? TGEN_XPORT_ERR_PROXY_VERSION : TGEN_XPORT_ERR_PROXY_STATUS);
        tgen_warning("connection from %s through socks proxy %s to %s failed: unsupported %s %i",
                tgenpeer_toString(transport->local), tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->remote),
                (buffer[0] != 0x05) ? "version" : "status",
                (buffer[0] != 0x05) ? (gint)buffer[0] : (gint)buffer[1]);
    }

    _tgentransport_changeState(transport, TGEN_XPORT_ERROR);
    return TGEN_EVENT_NONE;
}

TGenEvent tgentransport_onEvent(TGenTransport* transport, TGenEvent events) {
    TGEN_ASSERT(transport);
    if(!tgentransport_wantsEvents(transport)) {
        return TGEN_EVENT_NONE;
    }

    switch(transport->state) {
    case TGEN_XPORT_CONNECT: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            /* we are now connected and can send the socks init */
            transport->time.socketConnect = g_get_monotonic_time();
            if(transport->proxy) {
                /* continue with SOCKS handshake next */
                _tgentransport_changeState(transport, TGEN_XPORT_PROXY_INIT);
                /* process the next step */
                return tgentransport_onEvent(transport, events);
            } else {
                /* no proxy, this is a direct connection, we are all done */
                _tgentransport_changeState(transport, TGEN_XPORT_SUCCESS);
                return TGEN_EVENT_DONE;
            }
        }
    }

    case TGEN_XPORT_PROXY_INIT: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksInit(transport);
        }
    }

    case TGEN_XPORT_PROXY_CHOICE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksChoice(transport);
        }
    }

    case TGEN_XPORT_PROXY_REQUEST: {
        if(!(events & TGEN_EVENT_WRITE)) {
            return TGEN_EVENT_WRITE;
        } else {
            return _tgentransport_sendSocksRequest(transport);
        }
    }

    case TGEN_XPORT_PROXY_RESPONSE: {
        if(!(events & TGEN_EVENT_READ)) {
            return TGEN_EVENT_READ;
        } else {
            return _tgentransport_receiveSocksResponse(transport);
        }
    }

    case TGEN_XPORT_SUCCESS: {
        return TGEN_EVENT_DONE;
    }

    case TGEN_XPORT_ERROR:
    default: {
        return TGEN_EVENT_NONE;
    }
    }
}
