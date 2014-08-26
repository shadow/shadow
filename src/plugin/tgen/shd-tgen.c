/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGen {
    ShadowLogFunc log;
    ShadowCreateCallbackFunc createCallback;

    TGenGraph* actionGraph;

    gint epollD;
    gint serverD;
    GHashTable* transfers;

    TGenAction* startAction;
    gboolean hasEnded;
    guint numCompletedTransfers;
    guint64 totalBytesDownloaded;

    struct epoll_event* ee;

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

static void _tgen_initiateTransfer(TGen* tgen, TGenAction* action) {
    //todo
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
    //todo
}

static void _tgen_checkEndConditions(TGen* tgen, TGenAction* action) {
    //todo
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

    GQueue* nextActions = tgengraph_getNextActions(tgen->actionGraph, action);
    g_assert(nextActions);

    while(g_queue_get_length(nextActions) > 0) {
        _tgen_processAction(tgen, g_queue_pop_head(nextActions));
    }

    g_queue_free(nextActions);
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

void tgen_free(TGen* tgen) {
    TGEN_ASSERT(tgen);
    if (tgen->ee) {
        g_free(tgen->ee);
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

static void _tgen_acceptTransfer(TGen* tgen) {
    /* we have a new transfer request coming in on our listening socket
     * accept the socket and create the new transfer. */
    struct sockaddr_in peerAddress;
    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);
    memset(&peerAddress, 0, (size_t)addressLength);

    gint socketD = accept(tgen->serverD, (struct sockaddr*)&peerAddress, &addressLength);

    if(socketD > 0) {
        /* this transfer was initiated by the other end.
         * type, protocol, and size info will be sent to us later. */
        TGenTransfer* transfer = tgentransfer_newReactive(socketD,
                peerAddress.sin_addr.s_addr, peerAddress.sin_port);

        /* start watching whichever descriptor the transfer wants */
        gint watchD = tgentransfer_getEpollDescriptor(transfer);
        tgen->ee->events = EPOLLIN;
        tgen->ee->data.fd = watchD;
        gint result = epoll_ctl(tgen->epollD, EPOLL_CTL_ADD, watchD, tgen->ee);

        if(result == 0) {
            g_hash_table_replace(tgen->transfers, GINT_TO_POINTER(watchD), transfer);
        } else {
            tgen_critical("unable to accept new reactive transfer: problem watching "
                    "descriptor %i for events: epoll_ctl() errno %i",
                    watchD, errno);
            close(socketD);
            tgentransfer_free(transfer);
        }
    }
}

static void _tgen_closeTransfer(TGen* tgen, TGenTransfer* transfer) {
    gint watchD = tgentransfer_getEpollDescriptor(transfer);
    epoll_ctl(tgen->epollD, EPOLL_CTL_DEL, watchD, NULL);
    g_hash_table_remove(tgen->transfers, GINT_TO_POINTER(watchD));
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
                tgen_warning("can't find transfer for descriptor '%i'", desc);
                _tgen_closeTransfer(tgen, transfer);
                continue;
            }

            if(epevs[i].events & EPOLLIN) {
                tgentransfer_activate(transfer);
            }

            if(tgentransfer_isComplete(transfer)) {
                // todo update download and byte count here
                _tgen_closeTransfer(tgen, transfer);
            } else {
                //TODO
//                _tgen_adjustTransferEvents(tgen, transfer);
            }
        }
    }
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
