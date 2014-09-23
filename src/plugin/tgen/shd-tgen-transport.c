/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGenTransport {
    struct epoll_event epollE;
    gint epollD;

    TGenTransportProtocol protocol;
    gint tcpD;

    TGenPeer* peer;
    TGenPeer* proxy;
    gchar* string;

    TGenTransfer* activeTransfer;
    GHookFunc onTransferComplete;
    gpointer hookData;

    gint refcount;
    guint magic;
};

typedef struct _TGenTransportCallbackItem {
    TGenTransport* transport;
    TGenTransfer* transfer;
} TGenTransportCallbackItem;

static gchar* _tgentransport_toString(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    GString* stringBuffer = g_string_new(NULL);

    if(transport->proxy && transport->peer) {
        g_string_printf(stringBuffer, "[TCP-%i-%s-%s]", transport->tcpD,
                tgenpeer_toString(transport->proxy), tgenpeer_toString(transport->peer));
    } else if(transport->peer) {
        g_string_printf(stringBuffer, "[TCP-%i-%s]", transport->tcpD,
                tgenpeer_toString(transport->peer));
    } else {
        g_string_printf(stringBuffer, "[TCP-%i]", transport->tcpD);
    }

    return g_string_free(stringBuffer, FALSE);
}

TGenTransport* tgentransport_new(gint socketD, TGenPeer* proxy, TGenPeer* peer) {
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
    if(peer) {
        transport->peer = peer;
        tgenpeer_ref(peer);
    }
    if(proxy) {
        transport->proxy = proxy;
        tgenpeer_ref(proxy);
    }
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

    if(transport->peer) {
        tgenpeer_unref(transport->peer);
    }

    if(transport->proxy) {
        tgenpeer_unref(transport->proxy);
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

    gchar nameBuffer[256];
    memset(nameBuffer, 0, 256);
    gchar* name = (0 == gethostname(nameBuffer, 255)) ? &nameBuffer[0] : NULL;

    transport->activeTransfer = tgentransfer_new(name, &command);
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

static void _tgentransport_activateHelper(TGenTransport* transport, gint desc, gboolean in, gboolean out) {
    TGenTransferEventFlags flags = TGEN_EVENT_NONE;

    /* check if we need read flag */
    if(in && desc == transport->tcpD) {
        tgen_debug("transport %s is readable", transport->string);

        /* create a new transfer if we need to read command from the socket */
        if(!transport->activeTransfer) {
            transport->activeTransfer = tgentransfer_new(NULL, NULL);
        }

        flags |= TGEN_EVENT_READ;
    }

    /* check if we need write flag */
    if(out && desc == transport->tcpD) {
        tgen_debug("transport %s is writable", transport->string);

        /* if we have no transfer, we don't care about writing */
        if(transport->activeTransfer) {
            flags |= TGEN_EVENT_WRITE;
        }
    }

    /* activate the transfer */
    TGenTransferEventFlags status = TGEN_EVENT_NONE;
    if(flags) {
        status = tgentransfer_onSocketEvent(transport->activeTransfer, transport->tcpD, flags);
    }

    /* now check if we should update our epoll events */
    guint32 newEvents = 0;
    if(status & TGEN_EVENT_READ) {
        newEvents |= EPOLLIN;
    }
    if(status & TGEN_EVENT_WRITE) {
        newEvents |= EPOLLOUT;
    }

    if(newEvents != transport->epollE.events) {
        transport->epollE.events = newEvents;
        gint result = epoll_ctl(transport->epollD, EPOLL_CTL_MOD, transport->tcpD, &transport->epollE);
        if(result != 0) {
            tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                    transport->epollD, transport->tcpD, result, errno, g_strerror(errno));
            tgen_warning("epoll %i unable to change events on socket %i",
                    transport->epollD, transport->tcpD);
        }
    }

    /* check if the transfer finished */
    if(status & TGEN_EVENT_DONE) {
//        tgentransfer_unref(transport->activeTransfer);
//        transport->activeTransfer = NULL;
//        transport->onTransferComplete(transport->hookData);
//        transport->onTransferComplete = NULL;
//        transport->hookData = NULL;
    }
}

TGenTransferStatus tgentransport_activate(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    /* we need to make sure the transport doesn't get destroyed by the
     * driver after the transfer finishes but while still in use.
     * so we surround this function with ref/unref calls. */
    tgentransport_ref(transport);

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
            _tgentransport_activateHelper(transport, desc, in, out);
        }
    }

    tgentransport_unref(transport);
    return status;
}

gint tgentransport_getEpollDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->epollD;
}
