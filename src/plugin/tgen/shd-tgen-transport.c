/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGenTransport {
    TGenTransportProtocol protocol;
    gint refcount;

    gint epollD;
    gint tcpD;

    struct epoll_event epollE;

    TGenPeer peer;
    TGenPeer proxy;
    gchar* string;

    TGenTransfer* activeTransfer;
    GHookFunc onTransferComplete;
    gpointer hookData;

    guint magic;
};

typedef struct _TGenTransportCallbackItem {
    TGenTransport* transport;
    TGenTransfer* transfer;
} TGenTransportCallbackItem;

static gchar* _tgentransport_toString(TGenTransport* transport) {
    gchar* ipStringMemBuffer = g_malloc0(INET6_ADDRSTRLEN + 1);
    const gchar* ipString = inet_ntop(AF_INET, &(transport->peer.address),
            ipStringMemBuffer, INET6_ADDRSTRLEN);

    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "[TCP-%i-%s:%u]", transport->tcpD,
            ipString, transport->peer.port);

    g_free(ipStringMemBuffer);
    return g_string_free(stringBuffer, FALSE);
}

TGenTransport* tgentransport_new(gint socketD, const TGenPeer proxy, const TGenPeer peer) {
    if (socketD <= 0) {
        return NULL;
    }

    gint epollD = epoll_create(1);

    if (epollD < 0) {
        tgen_critical("epoll_create(): returned %i error %i: %s",
                epollD, errno, g_strerror(errno));
        close(epollD);
        return NULL;
    }

    /* start watching socket for incoming TGEN commands */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = socketD;
    gint result = epoll_ctl(epollD, EPOLL_CTL_ADD, socketD, &ev);
    if (result != 0) {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                epollD, socketD, result, errno, g_strerror(errno));
        return NULL;
    }

    TGenTransport* transport = g_new0(TGenTransport, 1);
    transport->magic = TGEN_MAGIC;
    transport->refcount = 1;

    transport->epollD = epollD;
    transport->tcpD = socketD;
    transport->protocol = TGEN_PROTOCOL_TCP;
    transport->peer = peer;
    transport->proxy = proxy;
    transport->epollE.events = ev.events;
    transport->epollE.data.fd = socketD;

    transport->string = _tgentransport_toString(transport);

    // TODO handle socks connection if proxy exists

    return transport;
}

static void _tgentransport_free(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(transport->tcpD > 0) {
        close(transport->tcpD);
        transport->tcpD = 0;
    }

    if(transport->string) {
        g_free(transport->string);
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

void tgentransport_setCommand(TGenTransport* transport, TGenTransferCommand command,
        GHookFunc onCommandComplete, gpointer hookData) {
    TGEN_ASSERT(transport);
    g_assert(!transport->activeTransfer);

    transport->activeTransfer = tgentransfer_new(&command);
    transport->onTransferComplete = onCommandComplete;
    transport->hookData = hookData;

    /* make sure we are waiting to write */
    if(!(transport->epollE.events & EPOLLOUT)) {
        transport->epollE.events |= EPOLLOUT;
        gint result = epoll_ctl(transport->epollD, EPOLL_CTL_MOD, transport->tcpD, &transport->epollE);
        if (result != 0) {
            tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                    transport->epollD, transport->tcpD, result, errno, g_strerror(errno));
        }
    }
}

static void tgentransport_cleanupActiveTransfer(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    tgentransfer_unref(transport->activeTransfer);
    transport->activeTransfer = NULL;
}

static gboolean _tgentransport_onReadable(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    tgen_debug("transport %s is readable", transport->string);

    if(!transport->activeTransfer) {
        /* create a new transfer, no command until we read it from the socket */
        transport->activeTransfer = tgentransfer_new(NULL);
    }

    gboolean keepReading = tgentransfer_onReadable(transport->activeTransfer, transport->tcpD);

    if(tgentransfer_isComplete(transport->activeTransfer)) {
        tgentransport_cleanupActiveTransfer(transport);
    }

    return keepReading;
}

static gboolean _tgentransport_onWritable(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    tgen_debug("transport %s is writable", transport->string);

    /* if we have no transfer, we don't care about writing */
    gboolean keepWriting = FALSE;

    if(transport->activeTransfer) {
        keepWriting = tgentransfer_onWritable(transport->activeTransfer, transport->tcpD);

        if(tgentransfer_isComplete(transport->activeTransfer)) {
            tgentransport_cleanupActiveTransfer(transport);
        }
    }

    return keepWriting;
}

TGenTransferStatus tgentransport_activate(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    TGenTransferStatus status;
    memset(&status, 0, sizeof(TGenTransferStatus));

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event epevs[10];

    /* collect all events that are ready */
    gint nfds = 1;
    while(nfds > 0) {
        nfds = epoll_wait(transport->epollD, epevs, 10, 0);
        if (nfds == -1) {
            tgen_critical("epoll_wait(): epoll %i returned %i error %i: %s",
                    transport->epollD, nfds, errno, g_strerror(errno));
        }

        /* activate correct component for every socket thats ready.
         * either our listening server socket has activity, or one of
         * our transfer sockets. */
        for (gint i = 0; i < nfds; i++) {
            gint desc = epevs[i].data.fd;
            gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
            gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

            gboolean changed = FALSE;

            if(in && desc == transport->tcpD) {
                gboolean keepReading = _tgentransport_onReadable(transport);
                if(!keepReading) {
                    transport->epollE.events &= ~EPOLLIN;
                    changed = TRUE;
                }
            }

            if(out && desc == transport->tcpD) {
                gboolean keepWriting = _tgentransport_onWritable(transport);
                if(!keepWriting) {
                   transport->epollE.events &= ~EPOLLOUT;
                   changed = TRUE;
               }
            }

            if(changed) {
                gint result = epoll_ctl(transport->epollD, EPOLL_CTL_MOD, transport->tcpD, &transport->epollE);
                if(result != 0) {
                    tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                            transport->epollD, transport->tcpD, result, errno, g_strerror(errno));
                    tgen_warning("epoll %i unable to change events on socket %i",
                            transport->epollD, transport->tcpD);
                }
            }
        }
    }

    return status;
}

gint tgentransport_getEpollDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->epollD;
}
