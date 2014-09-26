/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>
#include <glib/gstdio.h>

#include "shd-tgen.h"

struct _TGenDriver {
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

    gsize transferIDCounter;

    /* traffic statistics */
    guint totalTransfersCompleted;
    gsize totalBytesRead;
    gsize totalBytesWritten;

    guint refcount;
    guint magic;
};

typedef struct _TGenCallbackItem {
    TGenDriver* driver;
    TGenTransport* transport;
    TGenAction* action;
} TGenCallbackItem;

/* store a global pointer to the log func, so we can log in any
 * of our tgen modules without a pointer to the tgen struct */
ShadowLogFunc tgenLogFunc;

/* forward declaration */
static void _tgendriver_continueNextActions(TGenDriver* driver, TGenAction* action);

static guint64 _tgendriver_getCurrentTimeMillis() {
    struct timespec tp;
    memset(&tp, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, &tp);
    guint64 nowMillis = (((guint64)tp.tv_sec) * 1000) + (((guint64)tp.tv_nsec) / 1000000);
    return nowMillis;
}

static void _tgendriver_bootstrap(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    tgen_info("bootstrapping started");

    TGenAction* startAction = tgengraph_getStartAction(driver->actionGraph);

    /* we run our protocol over a single server socket/port */
    driver->serverD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (driver->serverD <= 0) {
        tgen_critical("problem bootstrapping: socket() returned %i", driver->serverD)
        driver->serverD = 0;
        return;
    }

    /* setup the listener address information */
    struct sockaddr_in listener;
    memset(&listener, 0, sizeof(struct sockaddr_in));
    listener.sin_family = AF_INET;
    listener.sin_addr.s_addr = htonl(INADDR_ANY);
    listener.sin_port = (in_port_t) tgenaction_getServerPort(startAction);

    /* bind the socket to the server port */
    gint result = bind(driver->serverD, (struct sockaddr *) &listener, sizeof(listener));
    if (result < 0) {
        tgen_critical("bind(): socket %i returned %i error %i: %s",
                driver->serverD, result, errno, g_strerror(errno));
        close(driver->serverD);
        driver->serverD = 0;
        return;
    }

    /* set as server listening socket */
    result = listen(driver->serverD, SOMAXCONN);
    if (result < 0) {
        tgen_critical("listen(): socket %i returned %i error %i: %s",
                driver->serverD, result, errno, g_strerror(errno));
        close(driver->serverD);
        driver->serverD = 0;
        return;
    }

    /* create an epoll descriptor so we can manage events */
    if (!driver->epollD) {
        driver->epollD = epoll_create(1);
        if (driver->epollD < 0) {
            tgen_critical("epoll_create(): returned %i error %i: %s",
                        driver->epollD, errno, g_strerror(errno));
            close(driver->epollD);
            driver->epollD = 0;
            close(driver->serverD);
            driver->serverD = 0;
            return;
        }
    }

    /* start watching our server socket */
    driver->ee->events = EPOLLIN;
    driver->ee->data.fd = driver->serverD;
    result = epoll_ctl(driver->epollD, EPOLL_CTL_ADD, driver->serverD, driver->ee);
    if (result != 0) {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                driver->epollD, driver->serverD, result, errno, g_strerror(errno));
        close(driver->epollD);
        driver->epollD = 0;
        close(driver->serverD);
        driver->serverD = 0;
        return;
    }

    /* if we got here, everything worked correctly! */
    driver->startAction = startAction;

    gchar ipStringBuffer[INET_ADDRSTRLEN + 1];
    memset(ipStringBuffer, 0, INET_ADDRSTRLEN + 1);
    inet_ntop(AF_INET, &listener.sin_addr.s_addr, ipStringBuffer, INET_ADDRSTRLEN);

    tgen_message("bootstrapped server listening at %s:%u", ipStringBuffer, ntohs(listener.sin_port));
}

static gboolean _tgendriver_openTransport(TGenDriver* driver, TGenTransport* transport) {
    gint watchD = tgentransport_getEpollDescriptor(transport);
    driver->ee->events = EPOLLIN;
    driver->ee->data.fd = watchD;
    gint result = epoll_ctl(driver->epollD, EPOLL_CTL_ADD, watchD, driver->ee);
    if(result == 0) {
        g_hash_table_replace(driver->transports, GINT_TO_POINTER(watchD), transport);
        return TRUE;
    } else {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                    driver->epollD, watchD, result, errno, g_strerror(errno));
        return FALSE;
    }
}

static gboolean _tgendriver_closeTransport(TGenDriver* driver, TGenTransport* transport) {
    gint watchD = tgentransport_getEpollDescriptor(transport);
    gint result = epoll_ctl(driver->epollD, EPOLL_CTL_DEL, watchD, NULL);
    if(result == 0) {
        g_hash_table_remove(driver->transports, GINT_TO_POINTER(watchD));
        return TRUE;
    } else {
        tgen_critical("epoll_ctl(): epoll %i socket %i returned %i error %i: %s",
                    driver->epollD, watchD, result, errno, g_strerror(errno));
        return FALSE;
    }
}

static void _tgendriver_transferCompleteCallback(TGenCallbackItem* item) {
    /* our transfer finished, close the socket */
    item->driver->totalTransfersCompleted++;
    _tgendriver_closeTransport(item->driver, item->transport);

    /* this only happens for transfers that our side initiated.
     * continue traversing the graph as instructed */
    _tgendriver_continueNextActions(item->driver, item->action);

    /* unref since the item object no longer holds the references */
    tgentransport_unref(item->transport);
    tgenaction_unref(item->action);
    tgendriver_unref(item->driver);
    /* cleanup */
    g_free(item);
}

static gint _tgendriver_createConnectedTCPSocket(TGenPeer* peer) {
    /* create the socket and get a socket descriptor */
    gint socketD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if (socketD < 0) {
        tgen_critical("socket(): returned %i error %i: %s",
                socketD, errno, g_strerror(errno));
        return 0;
    }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = tgenpeer_getNetworkIP(peer);
    server.sin_port = tgenpeer_getNetworkPort(peer);

    gint result = connect(socketD, (struct sockaddr *) &server, sizeof(server));

    /* nonblocking sockets means inprogress is ok */
    if (result < 0 && errno != EINPROGRESS) {
        tgen_critical("connect(): socket %i returned %i error %i: %s",
                        socketD, result, errno, g_strerror(errno));
        close(socketD);
        return 0;
    }

    return socketD;
}

static void _tgendriver_initiateTransfer(TGenDriver* driver, TGenAction* action) {
    TGenTransferType type;
    TGenTransportProtocol protocol;
    guint64 size;
    tgenaction_getTransferParameters(action, &type, &protocol, &size);

    /* the peer list of the transfer takes priority over the general start peer list
     * we must have a list of peers to transfer to one of them */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(driver->startAction);
    }
    g_assert(peers);

    /* select a random peer */
    TGenPeer* peer = tgenpool_getRandom(peers);
    g_assert(peer);

    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);

    /* create the new transport socket, etc */
    // TODO only create a new socket and transport if we dont already have one to this peer?
    gint socketD = _tgendriver_createConnectedTCPSocket(proxy ? proxy : peer);
    TGenTransport* transport = NULL;
    if (socketD > 0) {
        transport = tgentransport_new(socketD, proxy, peer);
    }

    if(transport) {
        /* track the transport */
        gboolean success = _tgendriver_openTransport(driver, transport);
        if(success) {
            tgen_info("created new transport socket %i", socketD)

            /* set a callback so we know how to continue when the transfer is done */
            GHookFunc cb = (GHookFunc) _tgendriver_transferCompleteCallback;

            TGenCallbackItem* item = g_new0(TGenCallbackItem, 1);
            tgendriver_ref(driver);
            item->driver = driver;
            tgenaction_ref(action);
            item->action = action;
            tgentransport_ref(transport);
            item->transport = transport;

            TGenTransferCommand command = {++(driver->transferIDCounter), type, size};
            tgentransport_setCommand(transport, command, cb, item);
        } else {
            gint watchD = tgentransport_getEpollDescriptor(transport);
            tgen_warning("unable to accept new transport: epoll %i unable to watch "
                          "descriptor %i for events", watchD, socketD);
            close(socketD);
            tgentransport_unref(transport);
        }
    } else {
        /* something failed during transfer initialization, move to the next action */
        tgen_warning("skipping failed transfer action");
        if(socketD > 0) {
            close(socketD);
        }
        _tgendriver_continueNextActions(driver, action);
    }
}

static void _tgendriver_acceptTransport(TGenDriver* driver) {
    /* we have a new transfer request coming in on our listening socket
     * accept the socket and create the new transfer. */
    struct sockaddr_in peerAddress;
    memset(&peerAddress, 0, sizeof(struct sockaddr_in));

    socklen_t addressLength = (socklen_t)sizeof(struct sockaddr_in);

    gint socketD = accept(driver->serverD, (struct sockaddr*)&peerAddress, &addressLength);
    if (socketD < 0) {
        tgen_critical("accept(): socket %i returned %i error %i: %s",
                driver->serverD, socketD, errno, g_strerror(errno));
        return;
    } else if(driver->hasEnded) {
        close(socketD);
        return;
    }

    /* this transfer was initiated by the other end.
     * type, and size info will be sent to us later. */
    TGenPeer* peer = tgenpeer_new(peerAddress.sin_addr.s_addr, peerAddress.sin_port);
    TGenTransport* transport = tgentransport_new(socketD, NULL, peer);
    tgenpeer_unref(peer);

    if(transport) {
        /* track the transport */
        gboolean success = _tgendriver_openTransport(driver, transport);
        if(success) {
            tgen_info("accepted new transport socket %i", socketD)
        } else {
            gint watchD = tgentransport_getEpollDescriptor(transport);
            tgen_warning("unable to accept new transport: epoll %i unable to watch "
                          "descriptor %i for events", watchD, socketD);
            close(socketD);
            tgentransport_unref(transport);
        }
    } else {
        /* something failed during transfer initialization, move to the next action */
        tgen_warning("skipping failed incoming transport");
    }
}

static void _tgendriver_pauseCallback(TGenCallbackItem* item) {
    /* continue next actions if possible */
    _tgendriver_continueNextActions(item->driver, item->action);

    /* cleanup */
    tgenaction_unref(item->action);
    tgendriver_unref(item->driver);
    g_free(item);
}

static void _tgendriver_initiatePause(TGenDriver* driver, TGenAction* action) {
    ShadowPluginCallbackFunc cb = (ShadowPluginCallbackFunc)_tgendriver_pauseCallback;
    TGenCallbackItem* item = g_new0(TGenCallbackItem, 1);
    tgendriver_ref(driver);
    item->driver = driver;
    tgenaction_ref(action);
    item->action = action;
    driver->createCallback(cb, item, (uint)tgenaction_getPauseTimeMillis(action));
}

static void _tgendriver_handleSynchronize(TGenDriver* driver, TGenAction* action) {
    // FIXME - actually implement synchronize feature - NOOP for now
    _tgendriver_continueNextActions(driver, action);
}

static void _tgendriver_checkEndConditions(TGenDriver* driver, TGenAction* action) {
    guint64 size = tgenaction_getEndSize(action);
    guint64 count = tgenaction_getEndCount(action);
    guint64 time = tgenaction_getEndTimeMillis(action);

    gsize totalBytes = driver->totalBytesRead + driver->totalBytesWritten;

    if(size > 0 && totalBytes >= (gsize)size) {
        driver->hasEnded = TRUE;
    } else if(count > 0 && driver->totalTransfersCompleted >= count) {
        driver->hasEnded = TRUE;
    } else if(time > 0) {
        guint64 nowMillis = _tgendriver_getCurrentTimeMillis();
        if(nowMillis >= time) {
            driver->hasEnded = TRUE;
        }
    }
}

static void _tgendriver_processAction(TGenDriver* driver, TGenAction* action) {
    switch(tgenaction_getType(action)) {
        case TGEN_ACTION_START: {
            /* slide through to the next actions */
            _tgendriver_continueNextActions(driver, action);
            break;
        }
        case TGEN_ACTION_TRANSFER: {
            _tgendriver_initiateTransfer(driver, action);
            break;
        }
        case TGEN_ACTION_SYNCHR0NIZE: {
            _tgendriver_handleSynchronize(driver, action);
            break;
        }
        case TGEN_ACTION_END: {
            _tgendriver_checkEndConditions(driver, action);
            _tgendriver_continueNextActions(driver, action);
            break;
        }
        case TGEN_ACTION_PAUSE: {
            _tgendriver_initiatePause(driver, action);
            break;
        }
        default: {
            tgen_warning("unrecognized action type");
            break;
        }
    }
}

static void _tgendriver_continueNextActions(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    if(driver->hasEnded) {
        return;
    }

    GQueue* nextActions = tgengraph_getNextActions(driver->actionGraph, action);
    g_assert(nextActions);

    while(g_queue_get_length(nextActions) > 0) {
        _tgendriver_processAction(driver, g_queue_pop_head(nextActions));
    }

    g_queue_free(nextActions);
}

static void _tgendriver_start(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    tgen_info("continuing from root start action");

    _tgendriver_continueNextActions(driver, driver->startAction);
}

void tgendriver_activate(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    if (!driver->startAction) {
        return;
    }

    /* collect the events that are ready */
    struct epoll_event epevs[10];
    gint nfds = epoll_wait(driver->epollD, epevs, 10, 0);
    if (nfds == -1) {
        tgen_warning("epoll_wait(): epoll %i returned %i error %i: %s",
                driver->epollD, nfds, errno, g_strerror(errno));
    }

    /* activate correct component for every socket thats ready.
     * either our listening server socket has activity, or one of
     * our transfer sockets. */
    for (gint i = 0; i < nfds; i++) {
        gint desc = epevs[i].data.fd;
        if (desc == driver->serverD) {
            /* listener socket should only be readable to indicate
             * it needs to accept a new socket */
            g_assert(epevs[i].events == EPOLLIN);

            /* handle the read event on our listener socket */
            _tgendriver_acceptTransport(driver);
        } else {
            TGenTransport* transport = g_hash_table_lookup(driver->transports,
                    GINT_TO_POINTER(desc));
            if (!transport) {
                tgen_warning("can't find transport for descriptor '%i', closing", desc);
                _tgendriver_closeTransport(driver, transport);
                continue;
            }

            /* transport epoll descriptor should only ever be readable */
            if(!(epevs[i].events & EPOLLIN)) {
                tgen_warning("child transport with descriptor '%i' is active without EPOLLIN, closing", desc);
                _tgendriver_closeTransport(driver, transport);
                tgentransport_unref(transport);
                continue;
            }

            TGenTransferStatus current = tgentransport_activate(transport);
            driver->totalBytesRead += current.bytesRead;
            driver->totalBytesWritten += current.bytesWritten;
        }
    }

    tgen_debug("total transfers=%u bytesread=%"G_GSIZE_FORMAT" byteswrite=%"G_GSIZE_FORMAT,
            driver->totalTransfersCompleted, driver->totalBytesRead, driver->totalBytesWritten);
}

static void _tgendriver_free(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    g_assert(driver->refcount <= 0);
    if (driver->ee) {
        g_free(driver->ee);
    }
    if (driver->transports) {
        g_hash_table_destroy(driver->transports);
    }
    if (driver->serverD) {
        close(driver->serverD);
    }
    if (driver->epollD) {
        close(driver->epollD);
    }
    if (driver->actionGraph) {
        tgengraph_free(driver->actionGraph);
    }
    driver->magic = 0;
    g_free(driver);
}

void tgendriver_ref(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    driver->refcount++;
}

void tgendriver_unref(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    if(--driver->refcount <= 0) {
        _tgendriver_free(driver);
    }
}

//static gchar* _tgendriver_makeTempFile() {
//    gchar nameBuffer[256];
//    memset(nameBuffer, 0, 256);
//    gethostname(nameBuffer, 255);
//
//    GString* templateBuffer = g_string_new("XXXXXX-shadow-tgen-");
//    g_string_append_printf(templateBuffer, "%s.xml", nameBuffer);
//
//    gchar* temporaryFilename = NULL;
//    gint openedFile = g_file_open_tmp(templateBuffer->str, &temporaryFilename, NULL);
//
//    g_string_free(templateBuffer, TRUE);
//
//    if(openedFile > 0) {
//        close(openedFile);
//        return g_strdup(temporaryFilename);
//    } else {
//        return NULL;
//    }
//}

TGenDriver* tgendriver_new(gint argc, gchar* argv[], ShadowLogFunc logf,
        ShadowCreateCallbackFunc callf) {
    tgenLogFunc = logf;

    /* argv[0] is program name, argv[1] should be config file */
    if (argc != 2) {
        tgen_warning("USAGE: %s path/to/tgen.xml", argv[0]);
        return NULL;
    }

    TGenGraph* graph = tgengraph_new(argv[1]);

//    if(argv[1] && g_str_has_prefix(argv[1], "<?xml")) {
//        /* argv contains the xml contents of the xml file */
//        gchar* tempPath = _tgendriver_makeTempFile();
//        GError* error = NULL;
//        gboolean success = g_file_set_contents(tempPath, argv[1], -1, &error);
//        if(success) {
//            graph = tgengraph_new(tempPath);
//        } else {
//            tgen_warning("error (%i) while generating temporary xml file: %s", error->code, error->message);
//        }
//        g_unlink(tempPath);
//        g_free(tempPath);
//    } else {
//        /* argv contains the apth of a graphml config file */
//        graph = tgengraph_new(argv[1]);
//    }

    if (graph) {
        tgen_message("traffic generator config file '%s' passed validation", argv[1]);
    } else {
        tgen_error("traffic generator config file '%s' failed validation", argv[1]);
        return NULL;
    }

    /* create the main driver object */
    TGenDriver* driver = g_new0(TGenDriver, 1);
    driver->magic = TGEN_MAGIC;
    driver->refcount = 1;

    driver->log = logf;
    driver->createCallback = callf;
    driver->actionGraph = graph;
    driver->transports = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
            (GDestroyNotify) tgentransport_unref);
    driver->ee = g_new0(struct epoll_event, 1);

    tgen_debug("set log function to %p, callback function to %p", logf, callf);

    /* setup our epoll descriptor and our server-side listener */
    _tgendriver_bootstrap(driver);

    /* the client-side transfers start as specified in the action */
    if (driver->startAction) {
        guint64 startMillis = tgenaction_getStartTimeMillis(driver->startAction);
        guint64 nowMillis = _tgendriver_getCurrentTimeMillis();

        if(startMillis > nowMillis) {
            driver->createCallback((ShadowPluginCallbackFunc)_tgendriver_start,
                    driver, (guint) (startMillis - nowMillis));
        } else {
            _tgendriver_start(driver);
        }
    }

    // TODO add heartbeat every 1 second

    return driver;
}

gint tgendriver_getEpollDescriptor(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->epollD;
}

gboolean tgendriver_hasStarted(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->startAction != NULL;
}

gboolean tgendriver_hasEnded(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->hasEnded;
}
