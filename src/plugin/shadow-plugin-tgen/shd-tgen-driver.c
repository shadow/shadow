/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>
#include <glib/gstdio.h>

#include "shd-tgen.h"

#define MAX_EVENTS_PER_IO_LOOP 100

struct _TGenDriver {
    /* our graphml dependency graph */
    TGenGraph* actionGraph;

    /* the starting action parsed from the action graph */
    TGenAction* startAction;
    gint64 startTimeMicros;

    /* TRUE iff a condition in any endAction event has been reached */
    gboolean clientHasEnded;
    /* the server only ends if an end time is specified */
    gboolean serverHasEnded;

    /* our I/O event manager. this holds refs to all of the transfers
     * and notifies them of I/O events on the underlying transports */
    TGenIO* io;

    /* each transfer has a unique id */
    gsize globalTransferCounter;

    /* traffic statistics */
    guint64 heartbeatTransfersCompleted;
    guint64 heartbeatTransferErrors;
    gsize heartbeatBytesRead;
    gsize heartbeatBytesWritten;
    guint64 totalTransfersCompleted;
    guint64 totalTransferErrors;
    gsize totalBytesRead;
    gsize totalBytesWritten;

    gint refcount;
    guint magic;
};

/* forward declaration */
static void _tgendriver_continueNextActions(TGenDriver* driver, TGenAction* action);
static void _tgendriver_processAction(TGenDriver* driver, TGenAction* action);

static gint64 _tgendriver_getCurrentTimeMillis() {
    return g_get_monotonic_time()/1000;
}

static void _tgendriver_onTransferComplete(TGenDriver* driver, TGenAction* action, gboolean wasSuccess) {
    TGEN_ASSERT(driver);

    /* our transfer finished, close the socket */
    if(wasSuccess) {
        driver->heartbeatTransfersCompleted++;
        driver->totalTransfersCompleted++;
    } else {
        driver->heartbeatTransferErrors++;
        driver->totalTransferErrors++;
    }

    /* this only happens for transfers that our side initiated.
     * continue traversing the graph as instructed */
    if(action) {
        _tgendriver_continueNextActions(driver, action);
    }
}

static void _tgendriver_onBytesTransferred(TGenDriver* driver, gsize bytesRead, gsize bytesWritten) {
    TGEN_ASSERT(driver);

    driver->totalBytesRead += bytesRead;
    driver->heartbeatBytesRead += bytesRead;
    driver->totalBytesWritten += bytesWritten;
    driver->heartbeatBytesWritten += bytesWritten;
}

static gboolean _tgendriver_onHeartbeat(TGenDriver* driver, gpointer nullData) {
    TGEN_ASSERT(driver);

    tgen_message("[driver-heartbeat] bytes-read=%"G_GSIZE_FORMAT" bytes-written=%"G_GSIZE_FORMAT
            " current-transfers-succeeded=%"G_GUINT64_FORMAT" current-transfers-failed=%"G_GUINT64_FORMAT
            " total-transfers-succeeded=%"G_GUINT64_FORMAT" total-transfers-failed=%"G_GUINT64_FORMAT,
            driver->heartbeatBytesRead, driver->heartbeatBytesWritten,
            driver->heartbeatTransfersCompleted, driver->heartbeatTransferErrors,
            driver->totalTransfersCompleted, driver->totalTransferErrors);

    driver->heartbeatTransfersCompleted = 0;
    driver->heartbeatTransferErrors = 0;
    driver->heartbeatBytesRead = 0;
    driver->heartbeatBytesWritten = 0;

    tgenio_checkTimeouts(driver->io);

    /* even if the client ended, we keep serving requests.
     * we are still running and the heartbeat timer still owns a driver ref.
     * do not cancel the timer */
    return FALSE;
}

static gboolean _tgendriver_onStartClientTimerExpired(TGenDriver* driver, gpointer nullData) {
    TGEN_ASSERT(driver);

    driver->startTimeMicros = g_get_monotonic_time();

    tgen_message("starting client using action graph '%s'",
            tgengraph_getGraphPath(driver->actionGraph));
    _tgendriver_continueNextActions(driver, driver->startAction);

    return TRUE;
}

static gboolean _tgendriver_onPauseTimerExpired(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    tgen_info("pause timer expired");

    /* continue next actions if possible */
    _tgendriver_continueNextActions(driver, action);
    /* timer was a one time event, so it can be canceled */
    return TRUE;
}

static void _tgendriver_onNewPeer(TGenDriver* driver, gint socketD, gint64 started, gint64 created, TGenPeer* peer) {
    TGEN_ASSERT(driver);

    /* we have a new peer connecting to our listening socket */
    if(driver->clientHasEnded) {
        close(socketD);
        return;
    }

    /* this connect was initiated by the other end.
     * transfer information will be sent to us later. */
    TGenTransport* transport = tgentransport_newPassive(socketD, started, created, peer,
            (TGenTransport_notifyBytesFunc) _tgendriver_onBytesTransferred, driver,
            (GDestroyNotify)tgendriver_unref);

    if(!transport) {
        tgen_warning("failed to initialize transport for incoming peer, skipping");
        return;
    }

    /* ref++ the driver for the transport notify func */
    tgendriver_ref(driver);

    /* default timeout after which we give up on transfer */
    guint64 defaultTimeout = tgenaction_getDefaultTimeoutMillis(driver->startAction);
    guint64 defaultStallout = tgenaction_getDefaultStalloutMillis(driver->startAction);

    /* a new transfer will be coming in on this transport */
    gsize count = ++(driver->globalTransferCounter);
    TGenTransfer* transfer = tgentransfer_new(NULL, count, TGEN_TYPE_NONE, 0, 0, 0,
            defaultTimeout, defaultStallout, transport,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onTransferComplete, driver, NULL,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(!transfer) {
        tgentransport_unref(transport);
        tgendriver_unref(driver);
        tgen_warning("failed to initialize transfer for incoming peer, skipping");
        return;
    }

    /* ref++ the driver for the transfer notify func */
    tgendriver_ref(driver);

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(driver->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our transport pointer reference, the transfer should hold one */
    tgentransport_unref(transport);
}

static void _tgendriver_initiateTransfer(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    /* the peer list of the transfer takes priority over the general start peer list
     * we must have a list of peers to transfer to one of them */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(driver->startAction);
    }

    if(!peers) {
        tgen_error("missing peers for transfer action; note that peers must be specified in "
                "either the start action, or in *every* transfer action");
    }

    TGenPeer* peer = tgenpool_getRandom(peers);
    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);

    TGenTransport* transport = tgentransport_newActive(proxy, peer,
            (TGenTransport_notifyBytesFunc) _tgendriver_onBytesTransferred, driver,
            (GDestroyNotify)tgendriver_unref);

    if(!transport) {
        tgen_warning("failed to initialize transport for transfer action, skipping");
        _tgendriver_continueNextActions(driver, action);
        return;
    }

    /* default timeout after which we give up on transfer */
    guint64 timeout = tgenaction_getDefaultTimeoutMillis(driver->startAction);
    guint64 stallout = tgenaction_getDefaultStalloutMillis(driver->startAction);

    /* ref++ the driver for the transport notify func */
    tgendriver_ref(driver);

    guint64 size = 0;
    guint64 ourSize = 0;
    guint64 theirSize = 0;
    TGenTransferType type = 0;
    /* this will only update timeout if there was a non-default timeout set for this transfer */
    tgenaction_getTransferParameters(action, &type, NULL, &size, &ourSize, &theirSize, &timeout, &stallout);

    /* the unique id of this vertex in the graph */
    const gchar* idStr = tgengraph_getActionIDStr(driver->actionGraph, action);
    gsize count = ++(driver->globalTransferCounter);

    /* a new transfer will be coming in on this transport. the transfer
     * takes control of the transport pointer reference. */
    TGenTransfer* transfer = tgentransfer_new(idStr, count, type, (gsize)size,
            (gsize)ourSize, (gsize)theirSize, timeout, stallout, transport,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onTransferComplete, driver, action,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgenaction_unref);

    if(!transfer) {
        tgentransport_unref(transport);
        tgendriver_unref(driver);
        tgen_warning("failed to initialize transfer for transfer action, skipping");
        _tgendriver_continueNextActions(driver, action);
        return;
    }

    /* ref++ the driver and action for the transfer notify func */
    tgendriver_ref(driver);
    tgenaction_ref(action);

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(driver->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our transport pointer reference, the transfer should hold one */
    tgentransport_unref(transport);
}

static gboolean _tgendriver_initiatePause(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    guint64 millisecondsPause = tgenaction_getPauseTimeMillis(action);

    /* create a timer to handle the pause action */
    TGenTimer* pauseTimer = tgentimer_new(millisecondsPause, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onPauseTimerExpired, driver, action,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgenaction_unref);

    if(!pauseTimer) {
        tgen_warning("failed to initialize timer for pause action, skipping");
        return FALSE;
    }

    tgen_info("set pause timer for %"G_GUINT64_FORMAT" milliseconds", millisecondsPause);

    /* ref++ the driver and action for the pause timer */
    tgendriver_ref(driver);
    tgenaction_ref(action);

    /* let the IO module handle timer reads, transfer the timer pointer reference */
    tgenio_register(driver->io, tgentimer_getDescriptor(pauseTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, pauseTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgendriver_handlePause(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    if(tgenaction_hasPauseTime(action)) {
        /* do a normal pause based on pause time */
        gboolean success = _tgendriver_initiatePause(driver, action);
        if(!success) {
            /* we have no timer set, lets just continue now so we dont stall forever */
            _tgendriver_continueNextActions(driver, action);
        }
    } else {
        /* do a 'synchronizing' pause where we wait until all incoming edges visit us */
        gboolean allVisited = tgenaction_incrementPauseVisited(action);
        if(allVisited) {
            _tgendriver_continueNextActions(driver, action);
        }
    }
}

static void _tgendriver_checkEndConditions(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    guint64 size = tgenaction_getEndSize(action);
    guint64 count = tgenaction_getEndCount(action);
    guint64 time = tgenaction_getEndTimeMillis(action);

    gsize totalBytes = driver->totalBytesRead + driver->totalBytesWritten;
    gint64 nowMillis = _tgendriver_getCurrentTimeMillis();
    gint64 timeLimit = (driver->startTimeMicros/1000) + (gint64)time;

    if(size > 0 && totalBytes >= (gsize)size) {
        driver->clientHasEnded = TRUE;
    } else if(count > 0 && driver->totalTransfersCompleted >= count) {
        driver->clientHasEnded = TRUE;
    } else if(time > 0) {
        if(nowMillis >= timeLimit) {
            driver->clientHasEnded = TRUE;
            driver->serverHasEnded = TRUE;
        }
    }

    tgen_debug("checked end conditions: hasEnded=%i "
            "bytes=%"G_GUINT64_FORMAT" limit=%"G_GUINT64_FORMAT" "
            "count=%"G_GUINT64_FORMAT" limit=%"G_GUINT64_FORMAT" "
            "time=%"G_GUINT64_FORMAT" limit=%"G_GUINT64_FORMAT,
            driver->clientHasEnded, totalBytes, size, driver->totalTransfersCompleted, count,
            nowMillis, timeLimit);
}

static void _tgendriver_processAction(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

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
        case TGEN_ACTION_END: {
            _tgendriver_checkEndConditions(driver, action);
            _tgendriver_continueNextActions(driver, action);
            break;
        }
        case TGEN_ACTION_PAUSE: {
            _tgendriver_handlePause(driver, action);
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

    if(driver->clientHasEnded) {
        return;
    }

    GQueue* nextActions = tgengraph_getNextActions(driver->actionGraph, action);
    g_assert(nextActions);

    while(g_queue_get_length(nextActions) > 0) {
        _tgendriver_processAction(driver, g_queue_pop_head(nextActions));
    }

    g_queue_free(nextActions);
}

void tgendriver_activate(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    if (!driver->startAction) {
        return;
    }

    tgen_debug("activating tgenio loop");

    gint numEventsProcessed = MAX_EVENTS_PER_IO_LOOP;

    while(numEventsProcessed >= MAX_EVENTS_PER_IO_LOOP) {
        numEventsProcessed = tgenio_loopOnce(driver->io, MAX_EVENTS_PER_IO_LOOP);
        tgen_debug("processed %i events out of the max allowed of %i", numEventsProcessed, MAX_EVENTS_PER_IO_LOOP);
    }

    tgen_debug("tgenio loop complete");
}

static void _tgendriver_free(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    g_assert(driver->refcount <= 0);

    tgen_info("freeing driver state");

    if(driver->io) {
        tgenio_unref(driver->io);
    }
    if(driver->actionGraph) {
        tgengraph_unref(driver->actionGraph);
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

static gboolean _tgendriver_startServerHelper(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    /* create the server that will listen for incoming connections */
    in_port_t serverPort = (in_port_t)tgenaction_getServerPort(driver->startAction);

    TGenServer* server = tgenserver_new(serverPort,
            (TGenServer_notifyNewPeerFunc)_tgendriver_onNewPeer, driver,
            (GDestroyNotify)tgendriver_unref);

    if(server) {
        /* the server is holding a ref to driver */
        tgendriver_ref(driver);

        /* now let the IO handler manage the server. transfer our server pointer reference
         * because it will be stored as a param in the IO object */
        gint socketD = tgenserver_getDescriptor(server);
        tgenio_register(driver->io, socketD, (TGenIO_notifyEventFunc)tgenserver_onEvent, NULL,
                server, (GDestroyNotify) tgenserver_unref);

        tgen_info("started server using descriptor %i", socketD);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgendriver_setStartClientTimerHelper(TGenDriver* driver, guint64 timerTime) {
    TGEN_ASSERT(driver);

    /* client will start in the future */
    TGenTimer* startTimer = tgentimer_new(timerTime, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onStartClientTimerExpired, driver, NULL,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(startTimer) {
        /* ref++ the driver since the timer is now holding a reference */
        tgendriver_ref(driver);

        /* let the IO module handle timer reads, transfer the timer pointer reference */
        gint timerD = tgentimer_getDescriptor(startTimer);
        tgenio_register(driver->io, timerD, (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL,
                startTimer, (GDestroyNotify)tgentimer_unref);

        tgen_info("set startClient timer using descriptor %i", timerD);
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _tgendriver_setHeartbeatTimerHelper(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    guint64 heartbeatPeriod = tgenaction_getHeartbeatPeriodMillis(driver->startAction);
    if(heartbeatPeriod == 0) {
        heartbeatPeriod = 1000;
    }

    /* start the heartbeat as a persistent timer event */
    TGenTimer* heartbeatTimer = tgentimer_new(heartbeatPeriod, TRUE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onHeartbeat, driver, NULL,
            (GDestroyNotify)tgendriver_unref, NULL);

    if(heartbeatTimer) {
        /* ref++ the driver since the timer is now holding a reference */
        tgendriver_ref(driver);

        /* let the IO module handle timer reads, transfer the timer pointer reference */
        gint timerD = tgentimer_getDescriptor(heartbeatTimer);
        tgenio_register(driver->io, timerD, (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL,
                heartbeatTimer, (GDestroyNotify)tgentimer_unref);

        tgen_info("set heartbeat timer using descriptor %i", timerD);
        return TRUE;
    } else {
        return FALSE;
    }
}

TGenDriver* tgendriver_new(TGenGraph* graph) {
    /* create the main driver object */
    TGenDriver* driver = g_new0(TGenDriver, 1);
    driver->magic = TGEN_MAGIC;
    driver->refcount = 1;

    driver->io = tgenio_new();

    tgengraph_ref(graph);
    driver->actionGraph = graph;
    driver->startAction = tgengraph_getStartAction(graph);

    /* start a heartbeat status message every second */
    if(!_tgendriver_setHeartbeatTimerHelper(driver)) {
        tgendriver_unref(driver);
        return NULL;
    }

    /* start a server to listen for incoming connections */
    if(!_tgendriver_startServerHelper(driver)) {
        tgendriver_unref(driver);
        return NULL;
    }

    /* only run the client if we have (non-start) actions we need to process */
    if(tgengraph_hasEdges(driver->actionGraph)) {
        /* the client-side transfers start as specified in the action.
         * this is a delay in milliseconds from now to start the client */
        guint64 delayMillis = tgenaction_getStartTimeMillis(driver->startAction);

        /* start our client after a timeout */
        if(!_tgendriver_setStartClientTimerHelper(driver, delayMillis)) {
            tgendriver_unref(driver);
            return NULL;
        }
    }

    return driver;
}

gint tgendriver_getEpollDescriptor(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return tgenio_getEpollDescriptor(driver->io);
}

gboolean tgendriver_hasEnded(TGenDriver* driver) {
    TGEN_ASSERT(driver);
    return driver->clientHasEnded;
}

/** Forward a request for all READ/WRITE events to the TGenIO */
void
tgendriver_giveAllEvents(TGenDriver *driver, gint descriptor)
{
    TGEN_ASSERT(driver);
    tgenio_giveAllEvents(driver->io, descriptor);
}
