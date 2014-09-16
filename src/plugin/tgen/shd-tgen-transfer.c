/*
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGenTransfer {
    TGenTransferType type;
    TGenTransferProtocol protocol;
	guint64 size;
	TGenPool* peerPool;

	gint epollD;
	gint tcpD;
	gint udpD;
	guint64 nBytesDownloaded;
	guint64 nBytesUploaded;

	gboolean isActive;
	TGenPeer peer;
	TGenPeer proxy;

	gchar* string;

	guint magic;
};

static gint _tgentransfer_createEpoll() {
    gint epollD = epoll_create(1);

    if (epollD < 0) {
        tgen_critical("error creating epoll: epoll_create() returned %i and errno is %i", epollD, errno);
        close(epollD);
        return 0;
    }

    return epollD;
}

static gint _tgentransfer_createConnectedTCPSocket(in_addr_t peerIP, in_port_t peerPort) {
    /* create the socket and get a socket descriptor */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (socketD < 0) {
        tgen_critical("error creating socket: socket() returned %i and errno is %i", socketD, errno);
        return 0;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = peerIP;
    server.sin_port = peerPort;

    gint result = connect(socketD, (struct sockaddr *) &server, sizeof(server));

    /* nonblocking sockets means inprogress is ok */
    if (result < 0 && errno != EINPROGRESS) {
        tgen_critical("error connecting socket: connect() returned %i and errno is %i", result, errno);
        close(socketD);
        return 0;
    }

    return socketD;
}

static gchar* _tgentransfer_toString(TGenTransfer* transfer) {
    gint sockd = 0;
    gchar* protocol = NULL;
    switch(transfer->protocol) {
        case TGEN_PROTOCOL_TCP: {
            protocol = "TCP";
            sockd = transfer->tcpD;
            break;
        }
        case TGEN_PROTOCOL_UDP: {
            protocol = "UDP";
            sockd = transfer->udpD;
            break;
        }
        case TGEN_PROTOCOL_PIPE: {
            protocol = "PIPE";
            break;
        }
        case TGEN_PROTOCOL_SOCKETPAIR: {
            protocol = "PAIR";
            break;
        }
        case TGEN_TYPE_NONE:
        default: {
            break;
        }
    }

    gchar* type = NULL;
    switch(transfer->type) {
        case TGEN_TYPE_GET: {
            type = "GET";
            break;
        }
        case TGEN_TYPE_PUT: {
            type = "PUT";
            break;
        }
        case TGEN_TYPE_NONE:
        default: {
            break;
        }
    }

    gchar* ipStringMemBuffer = g_malloc0(INET6_ADDRSTRLEN + 1);
    const gchar* ipString = inet_ntop(AF_INET, &(transfer->peer.address),
            ipStringMemBuffer, INET6_ADDRSTRLEN);

    GString* stringBuffer = g_string_new(NULL);
    g_string_printf(stringBuffer, "[%s-%i-%s-%lu-%s:%u]", protocol, sockd,
            type, transfer->size, ipString, transfer->peer.port);

    g_free(ipStringMemBuffer);
    return g_string_free(stringBuffer, FALSE);
}

TGenTransfer* tgentransfer_newReactive(gint socketD, in_addr_t peerIP, in_port_t peerPort) {
    /* create event listening facilities */
    gint epollD = _tgentransfer_createEpoll();
    if(epollD <= 0) {
        tgen_warning("error in _tgentransfer_createEpoll: unable to create epoll");
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

    TGenTransfer* transfer = g_new0(TGenTransfer, 1);
    transfer->magic = TGEN_MAGIC;

    transfer->epollD = epollD;
    transfer->tcpD = socketD;
    transfer->peer.address = peerIP;
    transfer->peer.port = peerPort;

    transfer->string = _tgentransfer_toString(transfer);

    return transfer;
}

TGenTransfer* tgentransfer_newActive(TGenTransferType type, TGenTransferProtocol protocol,
        guint64 size, TGenPool* peerPool, TGenPeer proxy) {
    /* create event listening facilities */
    gint epollD = _tgentransfer_createEpoll();
    if(epollD <= 0) {
        tgen_warning("error in _tgentransfer_createEpoll: unable to create epoll");
        return NULL;
    }

    /* select a random peer */
    const TGenPeer* peer = tgenpool_getRandom(peerPool);
    g_assert(peer);

    /* we connect through a proxy if given, otherwise directly to the peer */
    TGenPeer target = proxy.address > 0 && proxy.port > 0 ? proxy : *peer;

    /* create new base client TCP socket */
    gint socketD = _tgentransfer_createConnectedTCPSocket(target.address, target.port);
    if(socketD <= 0) {
        tgen_warning("error in _tgentransfer_createTCPSocket: unable to create tcp socket");
        return NULL;
    }

    /* start watching socket so we can start sending transfer commands */
    struct epoll_event ev;
    ev.events = EPOLLOUT;
    ev.data.fd = socketD;
    if (0 != epoll_ctl(epollD, EPOLL_CTL_ADD, socketD, &ev)) {
        tgen_warning("error in epoll_ctl: unable to watch socket %i with epoll %i", socketD, epollD);
        return NULL;
    }

    /* store the state */
	TGenTransfer* transfer = g_new0(TGenTransfer, 1);
	transfer->magic = TGEN_MAGIC;

	transfer->type = type;
	transfer->protocol = protocol;
	transfer->size = size;

	tgenpool_ref(peerPool);
	transfer->peerPool = peerPool;

    transfer->epollD = epollD;
    transfer->tcpD = socketD;
	transfer->isActive = TRUE;

	transfer->peer = *peer;
	transfer->proxy = proxy;

	transfer->string = _tgentransfer_toString(transfer);

	return transfer;
}

void tgentransfer_free(TGenTransfer* transfer) {
	TGEN_ASSERT(transfer);

	if(transfer->peerPool) {
		tgenpool_unref(transfer->peerPool);
	}

	if(transfer->string) {
	    g_free(transfer->string);
	}

	transfer->magic = 0;
	g_free(transfer);
}

static void _tgentransfer_onActiveReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("active transfer %s is readable", transfer->string);
}

static void _tgentransfer_onActiveWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("active transfer %s is writable", transfer->string);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = transfer->tcpD;
    if (0 != epoll_ctl(transfer->epollD, EPOLL_CTL_MOD, transfer->tcpD, &ev)) {
        tgen_warning("error in epoll_ctl: unable to watch socket %i with epoll %i",
                transfer->tcpD, transfer->epollD);
        return;
    }
}

static void _tgentransfer_onReactiveReadable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("reactive transfer %s is readable", transfer->string);
}

static void _tgentransfer_onReactiveWritable(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    //TODO
    tgen_debug("reactive transfer %s is writable", transfer->string);
}

TGenTransferStatus tgentransfer_activate(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);

    TGenTransferStatus status;
    memset(&status, 0, sizeof(TGenTransferStatus));

    /* storage for collecting events from our epoll descriptor */
    struct epoll_event epevs[10];

    /* collect all events that are ready */
    gint nfds = 1;
    while(nfds > 0) {
        nfds = epoll_wait(transfer->epollD, epevs, 10, 0);
        if (nfds == -1) {
            tgen_critical("error in transfer epoll_wait");
        }

        /* activate correct component for every socket thats ready.
         * either our listening server socket has activity, or one of
         * our transfer sockets. */
        for (gint i = 0; i < nfds; i++) {
            gint desc = epevs[i].data.fd;
            gboolean in = (epevs[i].events & EPOLLIN) ? TRUE : FALSE;
            gboolean out = (epevs[i].events & EPOLLOUT) ? TRUE : FALSE;

            if(in && desc == transfer->tcpD) {
                if(transfer->isActive) {
                    _tgentransfer_onActiveReadable(transfer);
                } else {
                    _tgentransfer_onReactiveReadable(transfer);
                }
            }

            if(out && desc == transfer->tcpD) {
                if(transfer->isActive) {
                    _tgentransfer_onActiveWritable(transfer);
                } else {
                    _tgentransfer_onReactiveWritable(transfer);
                }
            }
        }
    }

    return status;
}

gboolean tgentransfer_isComplete(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return FALSE;//transfer->nBytesCompleted >= transfer->size ? TRUE : FALSE;
}

gint tgentransfer_getEpollDescriptor(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return transfer->epollD;
}

guint64 tgentransfer_getSize(TGenTransfer* transfer) {
    TGEN_ASSERT(transfer);
    return transfer->size;
}
