/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGen {
    /* pointer to a logging function */
    ShadowLogFunc log;
    /* pointer to a function that issues a callback after timer expiration */
    ShadowCreateCallbackFunc createCallback;

    /* our graphml dependency graph */
    TGenGraph* actionGraph;

    /* the starting action parsed from the action graph */
    TGenAction* startAction;
    /* TRUE iff a condition in any endAction event has been reached */
    gboolean hasEnded;

    /* our main top-level epoll descriptor used to listen for events
     * on serverD and one descriptor (epoll or other) for each transfer */
    gint epollD;
    /* the main server that listens for new incoming connections */
    gint serverD;
    /* table that holds the transport objects, each transport is indexed
     * by the descriptor returned by that transport */
    GHashTable* transports;

    /* pointer to memory that facilitates the use of the epoll api */
    struct epoll_event* ee;

    /* traffic statistics */
    guint totalTransfersCompleted;
    guint64 totalBytesRead;
    guint64 totalBytesWritten;

    guint magic;
};

typedef struct _TGenCallbackItem {
    TGen* tgen;
    TGenTransport* transport;
    TGenAction* action;
} TGenCallbackItem;

/* store a global pointer to the log func, so we can log in any
 * of our tgen modules without a pointer to the tgen struct */
ShadowLogFunc tgenLogFunc;

/* forward declaration */
static void _tgen_continueNextActions(TGen* tgen, TGenAction* action);

static guint64 _tgen_getCurrentTimeMillis() {
    struct timespec tp;
    memset(&tp, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, &tp);
    guint64 nowMillis = (((guint64)tp.tv_sec) * 1000) + (((guint64)tp.tv_nsec) / 1000000);
    return nowMillis;
}

static void _tgen_bootstrap(TGen* tgen) {
    TGEN_ASSERT(tgen);

    tgen_info("bootstrapping started");

    TGenAction* startAction = tgengraph_getStartAction(tgen->actionGraph);

    /* we run our protocol over a single server socket/port */
    tgen->serverD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (tgen->serverD <= 0) {
        tgen_critical("problem bootstrapping: socket() returned %i", tgen->serverD)
        tgen->serverD = 0;
        return;
    }

    /* setup the listener address information */
    struct sockaddr_in listener;
    memset(&listener, 0, sizeof(listener));
    listener.sin_family = AF_INET;
    listener.sin_addr.s_addr = INADDR_ANY;
    listener.sin_port = (in_port_t) tgenaction_getServerPort(startAction);

    /* bind the socket to the server port */
    gint result = bind(tgen->serverD, (struct sockaddr *) &listener,
            sizeof(listener));
    if (result < 0) {
        tgen_critical("problem bootstrapping: bind() returned %i", result)
        close(tgen->serverD);
        tgen->serverD = 0;
        return;
    }

    /* set as server listening socket */
    result = listen(tgen->serverD, SOMAXCONN);
    if (result < 0) {
        tgen_critical("problem bootstrapping: listen() returned %i", result)
        close(tgen->serverD);
        tgen->serverD = 0;
        return;
    }

    /* create an epoll descriptor so we can manage events */
    if (!tgen->epollD) {
        tgen->epollD = epoll_create(1);
        if (tgen->epollD < 0) {
            tgen_critical("problem bootstrapping: epoll_create() returned %i", tgen->epollD)
            close(tgen->epollD);
            tgen->epollD = 0;
            close(tgen->serverD);
            tgen->serverD = 0;
            return;
        }
    }

    /* start watching our server socket */
    tgen->ee->events = EPOLLIN;
    tgen->ee->data.fd = tgen->serverD;
    result = epoll_ctl(tgen->epollD, EPOLL_CTL_ADD, tgen->serverD, tgen->ee);
    if (result != 0) {
        tgen_critical("problem bootstrapping: epoll_ctl() errno %i", errno);
        close(tgen->epollD);
        tgen->epollD = 0;
        close(tgen->serverD);
        tgen->serverD = 0;
        return;
    }

    /* if we got here, everything worked correctly! */
    tgen->startAction = startAction;

    gchar ipStringBuffer[INET_ADDRSTRLEN + 1];
    memset(ipStringBuffer, 0, INET_ADDRSTRLEN + 1);
    inet_ntop(AF_INET, &listener.sin_addr.s_addr, ipStringBuffer,
            INET_ADDRSTRLEN);

    tgen_message("bootstrapping complete: server listening at %s:%u", ipStringBuffer, listener.sin_port);
}

static gboolean _tgen_openTransport(TGen* tgen, TGenTransport* transport) {
    gint watchD = tgentransport_getEpollDescriptor(transport);
    tgen->ee->events = EPOLLIN;
    tgen->ee->data.fd = watchD;
    if(0 == epoll_ctl(tgen->epollD, EPOLL_CTL_ADD, watchD, tgen->ee)) {
        g_hash_table_replace(tgen->transports, GINT_TO_POINTER(watchD), transport);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgen_closeTransport(TGen* tgen, TGenTransport* transport) {
    gint watchD = tgentransport_getEpollDescriptor(transport);
    if(0 == epoll_ctl(tgen->epollD, EPOLL_CTL_DEL, watchD, NULL)) {
        g_hash_table_remove(tgen->transports, GINT_TO_POINTER(watchD));
        return TRUE;
    } else {
        return FALSE;
    }
}

static void _tgen_transferCompleteCallback(TGenCallbackItem* item) {
    // TODO what if tgen or action was destroyed in the meantime?

    tgentransport_unref(item->transport);

    /* this only happens for transfers that our side initiated.
     * continue traversing the graph as instructed */
    _tgen_continueNextActions(item->tgen, item->action);
    g_free(item);

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

static gint _tgen_createConnectedTCPSocket(in_addr_t peerIP, in_port_t peerPort) {
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

static void _tgen_initiateTransfer(TGen* tgen, TGenAction* action) {
    TGenTransferType type;
    TGenTransportProtocol protocol;
    guint64 size;
    tgenaction_getTransferParameters(action, &type, &protocol, &size);

    /* the peer list of the transfer takes priority over the general start peer list
     * we must have a list of peers to transfer to one of them */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(tgen->startAction);
    }
    g_assert(peers);

    /* select a random peer */
    const TGenPeer* peerPointer = tgenpool_getRandom(peers);
    g_assert(peerPointer);
    const TGenPeer peer = *peerPointer;

    const TGenPeer proxy = tgenaction_getSocksProxy(tgen->startAction);

    in_addr_t ip = proxy.address > 0 ? proxy.address : peer.address;
    in_addr_t port = proxy.port > 0 ? proxy.port : peer.port;

    /* create the new transport socket, etc */
    // TODO only create a new socket and transport if we dont already have one to this peer?
    gint socketD = _tgen_createConnectedTCPSocket(ip, port);
    TGenTransport* transport = NULL;
    if (socketD > 0) {
        transport = tgentransport_new(socketD, proxy, peer);
    }

    if(transport) {
        /* track the transport */
        gboolean success = _tgen_openTransport(tgen, transport);
        if(success) {
            tgen_info("created new transport socket %i")

            /* set a callback so we know how to continue when the transfer is done */
            GHookFunc cb = (GHookFunc) _tgen_transferCompleteCallback;
            TGenCallbackItem* item = g_new0(TGenCallbackItem, 1);
            item->tgen = tgen;
            item->transport = transport;
            item->action = action;
            TGenTransferCommand command = {type, size};
            tgentransport_setCommand(transport, command, cb, item);
        } else {
            gint watchD = tgentransport_getEpollDescriptor(transport);
            tgen_critical("unable to accept new transport: problem watching "
                    "descriptor %i for events: epoll_ctl() errno %i", watchD, errno);
            close(socketD);
            tgentransport_unref(transport);
        }
    } else {
        /* something failed during transfer initialization, move to the next action */
        tgen_warning("skipping failed transfer action");
        if(socketD > 0) {
            close(socketD);
        }
        _tgen_continueNextActions(tgen, action);
    }
}

static void _tgen_acceptTransport(TGen* tgen) {
    /* we have a new transfer request coming in on our listening socket
     * accept the socket and create the new transfer. */
    struct sockaddr_in peerAddress;
    memset(&peerAddress, 0, sizeof(struct sockaddr_in));

    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);

    gint socketD = accept(tgen->serverD, (struct sockaddr*)&peerAddress, &addressLength);
    if (socketD < 0) {
        tgen_critical("error accepting socketD: accept() returned %i and errno is %i", socketD, errno);
        return;
    } else if(tgen->hasEnded) {
        close(socketD);
        return;
    }

    /* this transfer was initiated by the other end.
     * type, and size info will be sent to us later. */
    TGenPeer peer;
    peer.address = peerAddress.sin_addr.s_addr;
    peer.port = peerAddress.sin_port;
    TGenPeer proxy;
    proxy.address = 0;
    proxy.port = 0;

    TGenTransport* transport = tgentransport_new(socketD, proxy, peer);
    if(transport) {
        /* track the transport */
        gboolean success = _tgen_openTransport(tgen, transport);
        if(success) {
            tgen_info("accepted new transport socket %i")
        } else {
            gint watchD = tgentransport_getEpollDescriptor(transport);
            tgen_critical("unable to accept new transport: problem watching "
                    "descriptor %i for events: epoll_ctl() errno %i", watchD, errno);
            close(socketD);
            tgentransport_unref(transport);
        }
    } else {
        /* something failed during transfer initialization, move to the next action */
        tgen_warning("skipping failed incoming transport");
    }
}

static void _tgen_pauseCallback(TGenCallbackItem* item) {
    // TODO what if tgen or action was destroyed in the meantime?
    _tgen_continueNextActions(item->tgen, item->action);
    g_free(item);
}

static void _tgen_initiatePause(TGen* tgen, TGenAction* action) {
    ShadowPluginCallbackFunc cb = (ShadowPluginCallbackFunc)_tgen_pauseCallback;
    TGenCallbackItem* item = g_new0(TGenCallbackItem, 1);
    item->tgen = tgen;
    item->action = action;
    tgen->createCallback(cb, item, (uint)tgenaction_getPauseTimeMillis(action));
}

static void _tgen_handleSynchronize(TGen* tgen, TGenAction* action) {
    //todo - actually implement synchronize feature
    _tgen_continueNextActions(tgen, action);
}

static void _tgen_checkEndConditions(TGen* tgen, TGenAction* action) {
    if(tgen->totalBytesRead + tgen->totalBytesWritten >= tgenaction_getEndSize(action)) {
        tgen->hasEnded = TRUE;
    } else if(tgen->totalTransfersCompleted >= tgenaction_getEndCount(action)) {
        tgen->hasEnded = TRUE;
    } else {
        guint64 nowMillis = _tgen_getCurrentTimeMillis();
        if(nowMillis >= tgenaction_getEndTimeMillis(action)) {
            tgen->hasEnded = TRUE;
        }
    }
}

static void _tgen_processAction(TGen* tgen, TGenAction* action) {
    switch(tgenaction_getType(action)) {
        case TGEN_ACTION_START: {
            /* slide through to the next actions */
            _tgen_continueNextActions(tgen, action);
            break;
        }
        case TGEN_ACTION_TRANSFER: {
            _tgen_initiateTransfer(tgen, action);
            break;
        }
        case TGEN_ACTION_SYNCHR0NIZE: {
            _tgen_handleSynchronize(tgen, action);
            break;
        }
        case TGEN_ACTION_END: {
            _tgen_checkEndConditions(tgen, action);
            break;
        }
        case TGEN_ACTION_PAUSE: {
            _tgen_initiatePause(tgen, action);
            break;
        }
        default: {
            tgen_warning("unrecognized action type");
            break;
        }
    }
}

static void _tgen_continueNextActions(TGen* tgen, TGenAction* action) {
    TGEN_ASSERT(tgen);

    if(tgen->hasEnded) {
        return;
    }

    GQueue* nextActions = tgengraph_getNextActions(tgen->actionGraph, action);
    g_assert(nextActions);

    while(g_queue_get_length(nextActions) > 0) {
        _tgen_processAction(tgen, g_queue_pop_head(nextActions));
    }

    g_queue_free(nextActions);
}

static void _tgen_start(TGen* tgen) {
    TGEN_ASSERT(tgen);

    tgen_info("continuing from root start action");

    _tgen_continueNextActions(tgen, tgen->startAction);
}

void tgen_activate(TGen* tgen) {
    TGEN_ASSERT(tgen);

    if (!tgen->startAction) {
        return;
    }

    /* collect the events that are ready */
    struct epoll_event epevs[10];
    gint nfds = epoll_wait(tgen->epollD, epevs, 10, 0);
    if (nfds == -1) {
        tgen_critical("error in client epoll_wait");
    }

    /* activate correct component for every socket thats ready.
     * either our listening server socket has activity, or one of
     * our transfer sockets. */
    for (gint i = 0; i < nfds; i++) {
        gint desc = epevs[i].data.fd;
        if (desc == tgen->serverD) {
            /* listener socket should only be readable to indicate
             * it needs to accept a new socket */
            g_assert(epevs[i].events == EPOLLIN);

            /* handle the read event on our listener socket */
            _tgen_acceptTransport(tgen);
        } else {
            TGenTransport* transport = g_hash_table_lookup(tgen->transports,
                    GINT_TO_POINTER(desc));
            if (!transport) {
                tgen_warning("can't find transport for descriptor '%i', closing", desc);
                _tgen_closeTransport(tgen, transport);
                continue;
            }

            /* transport epoll descriptor should only ever be readable */
            if(!(epevs[i].events & EPOLLIN)) {
                tgen_warning("child transport with descriptor '%i' is active without EPOLLIN, closing", desc);
                _tgen_closeTransport(tgen, transport);
                tgentransport_unref(transport);
                continue;
            }

            TGenTransferStatus current = tgentransport_activate(transport);
            tgen->totalBytesRead += current.bytesRead;
            tgen->totalBytesWritten += current.bytesWritten;
        }
    }

    if(tgen->hasEnded) {
        if(g_hash_table_size(tgen->transports) <= 0) {
            tgen_free(tgen);
        }
    }
}

void tgen_free(TGen* tgen) {
    TGEN_ASSERT(tgen);
    if (tgen->ee) {
        g_free(tgen->ee);
    }
    if (tgen->transports) {
        g_hash_table_destroy(tgen->transports);
    }
    if (tgen->serverD) {
        close(tgen->serverD);
    }
    if (tgen->epollD) {
        close(tgen->epollD);
    }
    if (tgen->actionGraph) {
        tgengraph_free(tgen->actionGraph);
    }
    tgen->magic = 0;
    g_free(tgen);
}

TGen* tgen_new(gint argc, gchar* argv[], ShadowLogFunc logf,
        ShadowCreateCallbackFunc callf) {
    tgenLogFunc = logf;

    /* argv[0] is program name, argv[1] should be config file */
    if (argc != 2) {
        tgen_warning("USAGE: %s path/to/tgen.xml", argv[0]);
        return NULL;
    }

    /* parse the graphml config file */
    TGenGraph* graph = tgengraph_new(argv[1]);

    if (graph) {
        tgen_message(
                "traffic generator config file '%s' passed validation", argv[1]);
    } else {
        tgen_warning(
                "traffic generator config file '%s' failed validation", argv[1]);
        return NULL;
    }

    /* create the main driver object */
    TGen* tgen = g_new0(TGen, 1);
    tgen->magic = TGEN_MAGIC;

    tgen->log = logf;
    tgen->createCallback = callf;
    tgen->actionGraph = graph;
    tgen->transports = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
            (GDestroyNotify) tgentransport_unref);
    tgen->ee = g_new0(struct epoll_event, 1);

    tgen_debug("set log function to %p, callback function to %p", logf, callf);

    /* setup our epoll descriptor and our server-side listener */
    _tgen_bootstrap(tgen);

    /* the client-side transfers start as specified in the action */
    if (tgen->startAction) {
        guint64 startMillis = tgenaction_getStartTimeMillis(tgen->startAction);
        guint64 nowMillis = _tgen_getCurrentTimeMillis();

        if(startMillis > nowMillis) {
            tgen->createCallback((ShadowPluginCallbackFunc)_tgen_start,
                    tgen, (guint) (startMillis - nowMillis));
        } else {
            _tgen_start(tgen);
        }
    }

    return tgen;
}

gint tgen_getEpollDescriptor(TGen* tgen) {
    TGEN_ASSERT(tgen);
    return tgen->epollD;
}

gboolean tgen_hasStarted(TGen* tgen) {
    TGEN_ASSERT(tgen);
    return tgen->startAction != NULL;
}

gboolean tgen_hasEnded(TGen* tgen) {
    TGEN_ASSERT(tgen);
    return tgen->hasEnded;
}
