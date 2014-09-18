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

    TGenPeer peer;
    TGenPeer proxy;

    gint transferIDCounter;
    GHashTable* transfers;

    gchar* string;

    guint magic;
};

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
        tgen_critical("error creating epoll: epoll_create() returned %i and errno is %i", epollD, errno);
        close(epollD);
        return NULL;
    }

    /* start watching socket for incoming TGEN commands */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = socketD;
    if (0 != epoll_ctl(epollD, EPOLL_CTL_ADD, socketD, &ev)) {
        tgen_warning("error in epoll_ctl: unable to watch socket %i with epoll %i", socketD, epollD);
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

    transport->transfers = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
                (GDestroyNotify) tgentransfer_unref);

    transport->string = _tgentransport_toString(transport);

    return transport;
}

static void _tgentransport_free(TGenTransport* transport) {
    TGEN_ASSERT(transport);

    if(transport->string) {
        g_free(transport->string);
    }
    if (transport->transfers) {
        g_hash_table_destroy(transport->transfers);
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

static void _tgentransport_onReadable(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    //TODO
    tgen_debug("transport %s is readable", transport->string);

//    if(tgentransfer_isComplete(transfer)) {
//        /* this transfer finished, clean it up */
//        tgen->totalTransfersCompleted++;
//        _tgen_closeTransfer(tgen, transfer);
//        tgentransfer_free(transfer);
//
//        /* if we are the client side of this transfer, initiate the next
//         * action from the action graph as appropriate */
//        TGenAction* transferAction = g_hash_table_lookup(tgen->activeTransferActions,
//                            GINT_TO_POINTER(desc));
//        if(transferAction) {
//            g_hash_table_remove(tgen->activeTransferActions, GINT_TO_POINTER(desc));
//            _tgen_continueNextActions(tgen, transferAction);
//        }
//    }
}

static void _tgentransport_onWritable(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    //TODO
    tgen_debug("transport %s is writable", transport->string);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = transport->tcpD;
    if (0 != epoll_ctl(transport->epollD, EPOLL_CTL_MOD, transport->tcpD, &ev)) {
        tgen_warning("error in epoll_ctl: unable to watch socket %i with epoll %i",
                transport->tcpD, transport->epollD);
        return;
    }
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
            tgen_critical("error in transport epoll_wait");
        }

        /* activate correct component for every socket thats ready.
         * either our listening server socket has activity, or one of
         * our transfer sockets. */
        for (gint i = 0; i < nfds; i++) {
            gint desc = epevs[i].data.fd;
            gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
            gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

            if(in && desc == transport->tcpD) {
                _tgentransport_onReadable(transport);
            }

            if(out && desc == transport->tcpD) {
                _tgentransport_onWritable(transport);
            }
        }
    }

    return status;
}

void tgentransport_addTransfer(TGenTransport* transport, TGenTransferType type, guint64 size,
        GHookFunc transferCompleteCallback, gpointer callbackData) {
    TGEN_ASSERT(transport);

    // TODO how to bootstrap the transfer
    gint transferID = transport->transferIDCounter++;
    TGenTransfer* transfer = tgentransfer_new(transferID, type, size);
    g_hash_table_replace(transport->transfers, GINT_TO_POINTER(transferID), transfer);
}

gint tgentransport_getEpollDescriptor(TGenTransport* transport) {
    TGEN_ASSERT(transport);
    return transport->epollD;
}
