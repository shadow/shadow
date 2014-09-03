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
    /* table that holds the currently active transfer actions, indexed
     * by the descriptor of the underlying transfer object */
    GHashTable* activeTransferActions;
    /* TRUE iff a condition in any endAction event has been reached */
    gboolean hasEnded;

    /* our main top-level epoll descriptor used to listen for events
     * on serverD and one descriptor (epoll or other) for each transfer */
    gint epollD;
    /* the main server that listens for new incomming connections */
    gint serverD;
    /* table that holds the transfer objects, each transfer is indexed
     * by the descriptor returned by that transfer */
    GHashTable* transfers;

    /* pointer to memory that facilitates the use of the epoll api */
    struct epoll_event* ee;

    /* traffic statistics */
    guint totalTransfersCompleted;
    guint64 totalBytesRead;
    guint64 totalBytesWritten;

    guint magic;
};

typedef struct _TGenPauseItem {
    TGen* tgen;
    TGenAction* action;
} TGenPauseItem;

/* store a global pointer to the log func, so we can log in any
 * of our tgen modules without a pointer to the tgen struct */
ShadowLogFunc tgenLogFunc;

/* forward declaration */
static void _tgen_continueNextActions(TGen* tgen, TGenAction* action);

static void _tgen_start(TGen* tgen) {
    TGEN_ASSERT(tgen);

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

static gboolean _tgen_openTransfer(TGen* tgen, TGenTransfer* transfer) {
    gint watchD = tgentransfer_getEpollDescriptor(transfer);
    tgen->ee->events = EPOLLIN;
    tgen->ee->data.fd = watchD;
    if(0 == epoll_ctl(tgen->epollD, EPOLL_CTL_ADD, watchD, tgen->ee)) {
        g_hash_table_replace(tgen->transfers, GINT_TO_POINTER(watchD), transfer);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgen_closeTransfer(TGen* tgen, TGenTransfer* transfer) {
    gint watchD = tgentransfer_getEpollDescriptor(transfer);
    if(0 == epoll_ctl(tgen->epollD, EPOLL_CTL_DEL, watchD, NULL)) {
        g_hash_table_remove(tgen->transfers, GINT_TO_POINTER(watchD));
        return TRUE;
    } else {
        return FALSE;
    }
}

static void _tgen_initiateTransfer(TGen* tgen, TGenAction* action) {
    TGenTransferType type;
    TGenTransferProtocol protocol;
    guint64 size;
    tgenaction_getTransferParameters(action, &type, &protocol, &size);

    /* the peer list of the transfer takes priority over the general start peer list */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(tgen->startAction);
    }
    /* we must have a list of peers to proceed to transfer to one of them */
    g_assert(peers);

    TGenPeer proxy = tgenaction_getSocksProxy(tgen->startAction);

    /* create the new transfer socket, etc */
    TGenTransfer* transfer = tgentransfer_newActive(type, protocol, size, peers, proxy);
    if(transfer) {
        /* track the transfer */
        _tgen_openTransfer(tgen, transfer);

        /* track the action so we know where to continue in the graph when its done */
        gint watchD = tgentransfer_getEpollDescriptor(transfer);
        g_hash_table_replace(tgen->activeTransferActions, GINT_TO_POINTER(watchD), action);
    } else {
        /* something failed during transfer initialization, move to the next action */
        tgen_warning("skipping failed transfer action");
        _tgen_continueNextActions(tgen, action);
    }
}

static void _tgen_pauseCallback(TGenPauseItem* item) {
    _tgen_continueNextActions(item->tgen, item->action);
    g_free(item);
}

static void _tgen_initiatePause(TGen* tgen, TGenAction* action) {
    ShadowPluginCallbackFunc cb = (ShadowPluginCallbackFunc)_tgen_pauseCallback;
    TGenPauseItem* item = g_new0(TGenPauseItem, 1);
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
        struct timespec tp;
        memset(&tp, 0, sizeof(struct timespec));
        clock_gettime(CLOCK_MONOTONIC, &tp);
        guint64 nowMillis = (((guint64)tp.tv_sec) * 1000) + (((guint64)tp.tv_nsec) / 1000000);

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

static void _tgen_acceptTransfer(TGen* tgen) {
    /* we have a new transfer request coming in on our listening socket
     * accept the socket and create the new transfer. */
    struct sockaddr_in peerAddress;
    memset(&peerAddress, 0, sizeof(struct sockaddr_in));

    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);
    gint socketD = accept(tgen->serverD, (struct sockaddr*)&peerAddress, &addressLength);

    if(tgen->hasEnded) {
        close(socketD);
        return;
    }

    if(socketD > 0) {
        /* this transfer was initiated by the other end.
         * type, protocol, and size info will be sent to us later. */
        TGenTransfer* transfer = tgentransfer_newReactive(socketD,
                peerAddress.sin_addr.s_addr, peerAddress.sin_port);

        /* start watching whichever descriptor the transfer wants */
        gboolean success = _tgen_openTransfer(tgen, transfer);
        if(!success) {
            gint watchD = tgentransfer_getEpollDescriptor(transfer);
            tgen_critical("unable to accept new reactive transfer: problem watching "
                    "descriptor %i for events: epoll_ctl() errno %i",
                    watchD, errno);
            close(socketD);
            tgentransfer_free(transfer);
        }
    }
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
            _tgen_acceptTransfer(tgen);
        } else {
            TGenTransfer* transfer = g_hash_table_lookup(tgen->transfers,
                    GINT_TO_POINTER(desc));
            if (!transfer) {
                tgen_warning("can't find transfer for descriptor '%i', closing", desc);
                _tgen_closeTransfer(tgen, transfer);
                continue;
            }

            /* transfer epoll descriptor should only ever be readable */
            if(!(epevs[i].events & EPOLLIN)) {
                tgen_warning("child transfer with descriptor '%i' is active without EPOLLIN, closing", desc);
                _tgen_closeTransfer(tgen, transfer);
                tgentransfer_free(transfer);
                continue;
            }

            TGenTransferStatus current = tgentransfer_activate(transfer);
            tgen->totalBytesRead += current.bytesRead;
            tgen->totalBytesWritten += current.bytesWritten;

            if(tgentransfer_isComplete(transfer)) {
                /* this transfer finished, clean it up */
                tgen->totalTransfersCompleted++;
                _tgen_closeTransfer(tgen, transfer);
                tgentransfer_free(transfer);

                /* if we are the client side of this transfer, initiate the next
                 * action from the action graph as appropriate */
                TGenAction* transferAction = g_hash_table_lookup(tgen->activeTransferActions,
                                    GINT_TO_POINTER(desc));
                if(transferAction) {
                    g_hash_table_remove(tgen->activeTransferActions, GINT_TO_POINTER(desc));
                    _tgen_continueNextActions(tgen, transferAction);
                }
            }
        }
    }

    if(tgen->hasEnded) {
        if(g_hash_table_size(tgen->transfers) <= 0) {
            tgen_free(tgen);
        }
    }
}

void tgen_free(TGen* tgen) {
    TGEN_ASSERT(tgen);
    if (tgen->ee) {
        g_free(tgen->ee);
    }
    if(tgen->activeTransferActions) {
        g_hash_table_destroy(tgen->activeTransferActions);
    }
    if (tgen->transfers) {
        g_hash_table_destroy(tgen->transfers);
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
    tgen->activeTransferActions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    tgen->transfers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
            (GDestroyNotify) tgentransfer_free);
    tgen->ee = g_new0(struct epoll_event, 1);

    tgen_debug("set log function to %p, callback function to %p", logf, callf);

    _tgen_start(tgen);

    if (tgen->startAction) {
        _tgen_continueNextActions(tgen, tgen->startAction);
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
