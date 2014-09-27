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

    /* our I/O event manager. this holds refs to all of the transfers
     * and notifies them of I/O events on the underlying transports */
    TGenIO* io;

    /* server to handle socket creation */
    TGenServer* server;
    gsize transferIDCounter;

    /* traffic statistics */
    guint heartbeatTransfersCompleted;
    gsize heartbeatBytesRead;
    gsize heartbeatBytesWritten;
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

static void _tgendriver_onTransferComplete(TGenDriver* driver, TGenAction* action, TGenTransfer* transfer) {
    TGEN_ASSERT(driver);

    /* our transfer finished, close the socket */
    driver->heartbeatTransfersCompleted++;
    driver->totalTransfersCompleted++;

    /* this only happens for transfers that our side initiated.
     * continue traversing the graph as instructed */
    if(action) {
        _tgendriver_continueNextActions(driver, action);
    }

    /* unref since the item object no longer holds the references */
    // FIXME mem
//    if(transfer) {
//        tgentransfer_unref(transfer);
//    }
//    if(action) {
//        tgenaction_unref(action);
//    }
//    tgendriver_unref(driver);
}

static void _tgendriver_onBytesTransferred(TGenDriver* driver, gsize bytesRead, gsize bytesWritten) {
    TGEN_ASSERT(driver);

    driver->totalBytesRead += bytesRead;
    driver->heartbeatBytesRead += bytesRead;
    driver->totalBytesWritten += bytesWritten;
    driver->heartbeatBytesWritten += bytesWritten;
}

static void _tgendriver_onNewPeer(TGenDriver* driver, gint socketD, TGenPeer* peer) {
    TGEN_ASSERT(driver);

    /* we have a new peer connecting to our listening socket */
    if(driver->hasEnded) {
        close(socketD);
        return;
    }

    /* this connect was initiated by the other end.
     * transfer information will be sent to us later. */
    TGenTransport* transport = tgentransport_newPassive(socketD, peer,
            (TGenTransport_onBytesFunc) _tgendriver_onBytesTransferred, driver);

    if(!transport) {
        tgen_warning("failed to initialize transport for incoming peer, skipping");
        return;
    }

    /* a new transfer will be coming in on this transport */
    gsize id = ++(driver->transferIDCounter);
    TGenTransfer* transfer = tgentransfer_new(id, TGEN_TYPE_NONE, 0, driver->io, transport,
            (TGenTransfer_onCompleteFunc)_tgendriver_onTransferComplete, driver, NULL);

    if(!transfer) {
        tgen_warning("failed to initialize transfer for incoming peer, skipping");
        return;
    }
}

static void _tgendriver_onHearbeat(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    tgen_message("[driver-heartbeat] transfers-completed=%u bytes-read=%"G_GSIZE_FORMAT" "
            "bytes-write=%"G_GSIZE_FORMAT, driver->heartbeatTransfersCompleted,
            driver->heartbeatBytesRead, driver->heartbeatBytesWritten);

    driver->heartbeatTransfersCompleted = 0;
    driver->heartbeatBytesRead = 0;
    driver->heartbeatBytesWritten = 0;

    if(driver->hasEnded && tgenio_getSize(driver->io) <= 1) {
        /* the server is the only one running */
        //TODO close server to unref driver?
        /* we are the only ref left, allow driver to be freed */
        tgendriver_unref(driver);
    } else {
        /* other refs exist so we are still running */
        ShadowPluginCallbackFunc cb = (ShadowPluginCallbackFunc)_tgendriver_onHearbeat;
        driver->createCallback(cb, driver, (uint)1000);
    }
}

static void _tgendriver_initiateTransfer(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    /* the peer list of the transfer takes priority over the general start peer list
     * we must have a list of peers to transfer to one of them */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(driver->startAction);
    }
    TGenPeer* peer = tgenpool_getRandom(peers);
    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);

    TGenTransport* transport = tgentransport_newActive(proxy, peer,
            (TGenTransport_onBytesFunc) _tgendriver_onBytesTransferred, driver);

    if(!transport) {
        tgen_warning("failed to initialize transport for transfer action, skipping");
        _tgendriver_continueNextActions(driver, action);
        return;
    }

    gsize size = 0;
    TGenTransferType type = 0;
    tgenaction_getTransferParameters(action, &type, NULL, &size);
    gsize id = ++(driver->transferIDCounter);

    /* a new transfer will be coming in on this transport */
    TGenTransfer* transfer = tgentransfer_new(id, type, size, driver->io, transport,
            (TGenTransfer_onCompleteFunc)_tgendriver_onTransferComplete, driver, action);

    if(!transfer) {
        tgen_warning("failed to initialize transfer for transfer action, skipping");
        _tgendriver_continueNextActions(driver, action);
        return;
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

    tgenio_loopOnce(driver->io);
}

static void _tgendriver_free(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    g_assert(driver->refcount <= 0);
    if(driver->server) {
        tgenserver_unref(driver->server);
    }
    if(driver->io) {
        tgenio_unref(driver->io);
    }
    if(driver->actionGraph) {
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
    tgen_debug("set log function to %p, callback function to %p", logf, callf);

    driver->io = tgenio_new();

    driver->actionGraph = graph;
    driver->startAction = tgengraph_getStartAction(graph);

    tgendriver_ref(driver);
    in_port_t serverPort = (in_port_t)tgenaction_getServerPort(driver->startAction);
    driver->server = tgenserver_new(driver->io, serverPort,
            (TGenServer_onNewPeerFunc)_tgendriver_onNewPeer, driver);

    /* the client-side (master) transfers start as specified in the action */
    guint64 startMillis = tgenaction_getStartTimeMillis(driver->startAction);
    guint64 nowMillis = _tgendriver_getCurrentTimeMillis();

    if(startMillis > nowMillis) {
        driver->createCallback((ShadowPluginCallbackFunc)_tgendriver_start,
                driver, (guint) (startMillis - nowMillis));
    } else {
        _tgendriver_start(driver);
    }

    /* add another ref and start the heartbeat event loop */
    tgendriver_ref(driver);
    _tgendriver_onHearbeat(driver);

    return driver;
}

gint tgendriver_getEpollDescriptor(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return tgenio_getEpollDescriptor(driver->io);
}

gboolean tgendriver_hasStarted(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->startAction != NULL;
}

gboolean tgendriver_hasEnded(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->hasEnded;
}
