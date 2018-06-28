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
static gboolean _tgendriver_onStartClientTimerExpired(TGenDriver* driver, gpointer nullData);
static gboolean _tgendriver_onPauseTimerExpired(TGenDriver* driver, TGenAction* action);
static gboolean _tgendriver_onGeneratorTimerExpired(TGenDriver* driver, TGenGenerator* generator);
static void _tgendriver_continueNextActions(TGenDriver* driver, TGenAction* action);

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

static void _tgendriver_onGeneratorTransferComplete(TGenDriver* driver, TGenGenerator* generator, gboolean wasSuccess) {
    TGEN_ASSERT(driver);

    _tgendriver_onTransferComplete(driver, NULL, wasSuccess);
    tgengenerator_onTransferCompleted(generator);

    if(tgengenerator_isDoneGenerating(generator) &&
            tgengenerator_getNumOutstandingTransfers(generator) <= 0) {
        tgen_info("Model action complete, continue to next action");
        TGenAction* action = tgengenerator_getModelAction(generator);
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
            defaultTimeout, defaultStallout, NULL, NULL, driver->io, transport,
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

/* this should only be called with action of type start, model, or transfer */
static TGenPeer* _tgendriver_getRandomPeer(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    /* the peer list of the action takes priority over the general start peer list
     * we must have a list of peers to transfer to one of them */
    TGenPool* peers = tgenaction_getPeers(action);
    if (!peers) {
        peers = tgenaction_getPeers(driver->startAction);
    }

    if(!peers) {
        /* FIXME we should handle this more gracefully than error */
        tgen_error("missing peers for transfer action; note that peers must be specified in "
                "either the start action, or in *every* transfer action");
    }

    return tgenpool_getRandom(peers);
}

static gboolean _tgendriver_createNewActiveTransfer(TGenDriver* driver,
        TGenTransferType type, TGenPeer* peer,
        guint64 size, guint64 ourSize, guint64 theirSize,
        guint64 timeout, guint64 stallout,
        gchar* localSchedule, gchar* remoteSchedule,
        gchar* socksUsername, gchar* socksPassword,
        const gchar* actionIDStr,
        TGenTransfer_notifyCompleteFunc onComplete,
        gpointer callbackArg1, gpointer callbackArg2,
        GDestroyNotify arg1Destroy, GDestroyNotify arg2Destroy) {
    TGEN_ASSERT(driver);

    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);
    if(timeout == 0) {
        timeout = tgenaction_getDefaultTimeoutMillis(driver->startAction);
    }
    if(stallout == 0) {
        stallout = tgenaction_getDefaultStalloutMillis(driver->startAction);
    }

    /* create the transport connection over which we can start a transfer */
    TGenTransport* transport = tgentransport_newActive(proxy, socksUsername, socksPassword, peer,
            (TGenTransport_notifyBytesFunc) _tgendriver_onBytesTransferred, driver,
            (GDestroyNotify)tgendriver_unref);

    if(transport) {
        /* ref++ the driver because the transport object is holding a ref to it
         * as a generic callback parameter for the notify function callback */
        tgendriver_ref(driver);
    } else {
        tgen_warning("failed to initialize transport for active transfer");
        return FALSE;
    }

    /* get transfer counter id */
    gsize count = ++(driver->globalTransferCounter);

    /* a new transfer will be coming in on this transport. the transfer
     * takes control of the transport pointer reference. */
    TGenTransfer* transfer = tgentransfer_new(actionIDStr, count, type, (gsize)size,
            (gsize)ourSize, (gsize)theirSize, timeout, stallout,
            localSchedule, remoteSchedule, driver->io, transport,
            onComplete, callbackArg1, callbackArg2, arg1Destroy, arg2Destroy);

    if(!transfer) {
        /* the transport was created, but we failed to create the transfer.
         * so we should clean up the transport since we no longer need it */
        tgentransport_unref(transport);

        /* XXX Rob:
         * I think the transport unref will call the driver unref function that
         * we passed as the destroy function, so I'm not sure why or if we need to
         * do it again here. Leaving it here for now. */
        tgendriver_unref(driver);

        tgen_warning("failed to initialize active transfer");
        return FALSE;
    }

    /* now let the IO handler manage the transfer. our transfer pointer reference
     * will be held by the IO object */
    tgenio_register(driver->io, tgentransport_getDescriptor(transport),
            (TGenIO_notifyEventFunc)tgentransfer_onEvent,
            (TGenIO_notifyCheckTimeoutFunc) tgentransfer_onCheckTimeout,
            transfer, (GDestroyNotify)tgentransfer_unref);

    /* release our local transport pointer ref (from when we initialized the new transport)
     * because the transfer now owns it and holds the ref */
    tgentransport_unref(transport);

    return TRUE;
}

static void _tgendriver_initiateTransfer(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    TGenPeer* peer = _tgendriver_getRandomPeer(driver, action);
    TGenTransferType type = 0;

    guint64 size = 0;
    guint64 ourSize = 0;
    guint64 theirSize = 0;

    guint64 timeout = 0;
    guint64 stallout = 0;

    gchar* localSchedule = NULL;
    gchar* remoteSchedule = NULL;

    /* if timeout is 0, we fall back to the start action timeout in the
     * _tgendriver_createNewActiveTransfer function */
    tgenaction_getTransferParameters(action, &type, NULL, &size, &ourSize,
            &theirSize, &timeout, &stallout, &localSchedule, &remoteSchedule);

    const gchar* actionIDStr = tgengraph_getActionIDStr(driver->actionGraph, action);

    /* socks username and password are populated if given in the transfer action. */
    gchar* socksUsername = NULL;
    gchar* socksPassword = NULL;
    tgenaction_getSocksParams(action, &socksUsername, &socksPassword);

    gboolean isSuccess = _tgendriver_createNewActiveTransfer(driver, type, peer,
            size, ourSize, theirSize, timeout, stallout,
            localSchedule, remoteSchedule, socksUsername, socksPassword,
            actionIDStr,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onTransferComplete,
            driver, action,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgenaction_unref);

    if(isSuccess) {
        /* ref++ the driver and action because the transfer object is holding refs
         * as generic callback parameters for the notify function callback.
         * these will get unref'd when the transfer finishes. */
        tgendriver_ref(driver);
        tgenaction_ref(action);
    } else {
       tgen_warning("skipping failed transfer action and continuing to the next action");
       _tgendriver_continueNextActions(driver, action);
    }
}

static gboolean _tgendriver_setGeneratorDelayTimer(TGenDriver* driver, TGenGenerator* generator,
        guint64 delayTimeUSec) {
    TGEN_ASSERT(driver);

    /* create a timer to handle so we can delay before starting the next transfer */
    TGenTimer* generatorTimer = tgentimer_new(delayTimeUSec, FALSE,
            (TGenTimer_notifyExpiredFunc)_tgendriver_onGeneratorTimerExpired, driver, generator,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgengenerator_unref);

    if(!generatorTimer) {
        tgen_warning("failed to initialize timer for model action");
        return FALSE;
    }

    tgen_info("set generator delay timer for %"G_GUINT64_FORMAT" microseconds", delayTimeUSec);

    /* ref++ the driver and generator for the refs held in the timer */
    tgendriver_ref(driver);
    tgengenerator_ref(generator);

    /* let the IO module handle timer reads, transfer the timer pointer reference */
    tgenio_register(driver->io, tgentimer_getDescriptor(generatorTimer),
            (TGenIO_notifyEventFunc)tgentimer_onEvent, NULL, generatorTimer,
            (GDestroyNotify)tgentimer_unref);

    return TRUE;
}

static void _tgendriver_generateNextTransfer(TGenDriver* driver, TGenGenerator* generator) {
    TGEN_ASSERT(driver);

    TGenAction* action = tgengenerator_getModelAction(generator);

    /* if these strings are non-null following this call, we own and must free them */
    gchar* localSchedule = NULL;
    gchar* remoteSchedule = NULL;
    guint64 delayTimeUSec = 0;
    gboolean shouldCreateStream = tgengenerator_generateStream(generator,
            &localSchedule, &remoteSchedule, &delayTimeUSec);

    if(!shouldCreateStream) {
       /* the generator reached the end of the streams for this flow,
        * so the action is now complete. */
        tgen_info("Generator reached end state after generating %u streams and %u packets",
                tgengenerator_getNumStreamsGenerated(generator),
                tgengenerator_getNumPacketsGenerated(generator));

        guint numOutstanding = tgengenerator_getNumOutstandingTransfers(generator);
        tgen_info("Generator has %u outstanding transfers", numOutstanding);

        if(tgengenerator_isDoneGenerating(generator) && numOutstanding <= 0) {
            tgen_info("Model action complete, continue to next action");
            _tgendriver_continueNextActions(driver, action);
        } else {
            tgen_info("Model action will be complete once all outstanding transfers finish");
        }

        tgengenerator_unref(generator);
        return;
    }

    /* we need to create a new transfer according to the schedules from the generator */

    TGenPeer* peer = _tgendriver_getRandomPeer(driver, action);
    const gchar* actionIDStr = tgengraph_getActionIDStr(driver->actionGraph, action);

    /* get socks user and password, these will remain null if not provided by user */
    gchar* socksUsername = NULL;
    gchar* socksPassword = NULL;
    tgenaction_getSocksParams(action, &socksUsername, &socksPassword);

    /* Create the schedule type transfer. The sizes will be computed from the
     * schedules, and timeout and stallout will be taken from the default start vertex.
     * We pass a NULL action, because we don't want to continue in the action graph
     * when this transfer completes (we continue when the generator is done). */
    gboolean isSuccess = _tgendriver_createNewActiveTransfer(driver, TGEN_TYPE_SCHEDULE, peer,
            0, 0, 0, 0, 0,
            localSchedule, remoteSchedule,
            socksUsername, socksPassword,
            actionIDStr,
            (TGenTransfer_notifyCompleteFunc)_tgendriver_onGeneratorTransferComplete,
            driver, generator,
            (GDestroyNotify)tgendriver_unref, (GDestroyNotify)tgengenerator_unref);

    if(isSuccess) {
        /* ref++ the driver and generator because the transfer object is holding refs
         * as generic callback parameters for the notify function callback.
         * these will get unref'd when the transfer finishes. */
        tgendriver_ref(driver);
        tgengenerator_ref(generator);
        tgengenerator_onTransferCreated(generator);
    } else {
       tgen_warning("we failed to create a transfer in model action, "
               "delaying %"G_GUINT64_FORMAT" microseconds before the next try",
               delayTimeUSec);
    }

    isSuccess = _tgendriver_setGeneratorDelayTimer(driver, generator, delayTimeUSec);

    if(!isSuccess) {
        tgen_warning("Failed to set generator delay timer for %"G_GUINT64_FORMAT" "
                "microseconds. Stopping generator now and skipping to next action.",
                delayTimeUSec);
        if(tgengenerator_isDoneGenerating(generator) &&
                tgengenerator_getNumOutstandingTransfers(generator) <= 0) {
            tgen_info("Model action complete, continue to next action");
            _tgendriver_continueNextActions(driver, action);
        }
        tgengenerator_unref(generator);
    }

    tgen_info("successfully generated new transfer to peer %s", tgenpeer_toString(peer));

    if(localSchedule) {
        g_free(localSchedule);
    }
    if(remoteSchedule) {
        g_free(remoteSchedule);
    }
}

static void _tgendriver_initiateGenerator(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    TGenPeer* proxy = tgenaction_getSocksProxy(driver->startAction);

    /* these strings are owned by the action and we should not free them */
    gchar* streamModelPath = NULL;
    gchar* packetModelPath = NULL;
    tgenaction_getModelPaths(action, &streamModelPath, &packetModelPath);

    TGenGenerator* generator = tgengenerator_new(streamModelPath, packetModelPath, action);

    if(!generator) {
        tgen_warning("failed to initialize generator for model action, skipping");
        _tgendriver_continueNextActions(driver, action);
        return;
    }

    /* start generating transfers! */
    _tgendriver_generateNextTransfer(driver, generator);
}

static gboolean _tgendriver_initiatePause(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    guint64 millisecondsPause = tgenaction_getPauseTimeMillis(action);
    guint64 microsecondsPause = millisecondsPause * 1000;

    /* create a timer to handle the pause action */
    TGenTimer* pauseTimer = tgentimer_new(microsecondsPause, FALSE,
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
        case TGEN_ACTION_MODEL: {
            _tgendriver_initiateGenerator(driver, action);
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

static gboolean _tgendriver_setStartClientTimerHelper(TGenDriver* driver) {
    TGEN_ASSERT(driver);

    /* this is a delay in milliseconds from now to start the client */
    guint64 delayMillis = tgenaction_getStartTimeMillis(driver->startAction);
    guint64 pauseTimeMicros = delayMillis * 1000;

    /* client will start in the future */
    TGenTimer* startTimer = tgentimer_new(pauseTimeMicros, FALSE,
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
    guint64 microsecondsPause = heartbeatPeriod * 1000;

    /* start the heartbeat as a persistent timer event */
    TGenTimer* heartbeatTimer = tgentimer_new(microsecondsPause, TRUE,
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
        /* the client-side transfers start as specified in the graph.
         * start our client after a timeout */
        if(!_tgendriver_setStartClientTimerHelper(driver)) {
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

static gboolean _tgendriver_onStartClientTimerExpired(TGenDriver* driver, gpointer nullData) {
    TGEN_ASSERT(driver);

    driver->startTimeMicros = g_get_monotonic_time();

    tgen_message("starting client using action graph '%s'",
            tgengraph_getGraphPath(driver->actionGraph));
    _tgendriver_continueNextActions(driver, driver->startAction);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgendriver_onPauseTimerExpired(TGenDriver* driver, TGenAction* action) {
    TGEN_ASSERT(driver);

    tgen_info("pause timer expired");

    /* continue next actions if possible */
    _tgendriver_continueNextActions(driver, action);
    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

static gboolean _tgendriver_onGeneratorTimerExpired(TGenDriver* driver, TGenGenerator* generator) {
    TGEN_ASSERT(driver);

    tgen_info("generator timer expired");
    _tgendriver_generateNextTransfer(driver, generator);

    /* timer was a one time event, so it can be canceled and freed */
    return TRUE;
}

