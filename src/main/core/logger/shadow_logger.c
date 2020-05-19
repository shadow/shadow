/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/core/logger/shadow_logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "main/core/logger/log_record.h"
#include "main/core/logger/logger_helper.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/host.h"
#include "main/routing/address.h"
#include "main/utility/count_down_latch.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

/* this stores thread-specific data for each "worker" thread (the threads that
 * are running the virtual nodes) */
typedef struct _LoggerThreadData LoggerThreadData;
struct _LoggerThreadData {
    /* keep wall time without relying on main logger data */
    GTimer* runTimer;

    /* local temporary store for this threads log records */
    GQueue* localRecordBundle;

    /* remote queue over which to send helper thread messages */
    GAsyncQueue* remoteLogHelperMailbox;
    MAGIC_DECLARE;
};

/* manages the logging of messages among multiple worker threads */
struct _ShadowLogger {
    Logger base;

    GTimer* runTimer;

    /* the level below which we filter messages */
    LogLevel filterLevel;

    /* if the logger should cache messages before writing for performance */
    gboolean shouldBuffer;
    gdouble lastTimespan;

    /* helper to sort messages and handle file i/o */
    pthread_t helper;
    GAsyncQueue* helperCommands;
    CountDownLatch* helperLatch;

    /* store map of other threads that will call logging functions to
     * thread-specific data */
    GHashTable* threadToDataMap;

    /* for memory management */
    gint referenceCount;
    MAGIC_DECLARE;
};

static LoggerThreadData* _loggerthreaddata_new(GTimer* loggerTimer) {
    LoggerThreadData* threadData = g_new0(LoggerThreadData, 1);
    MAGIC_INIT(threadData);

    threadData->runTimer = g_timer_new();

    threadData->localRecordBundle = g_queue_new();
    threadData->remoteLogHelperMailbox = g_async_queue_new();

    return threadData;
}

static void _loggerthreaddata_free(LoggerThreadData* threadData) {
    MAGIC_ASSERT(threadData);

    /* free any remaining log records and the record queue */
    LogRecord* record = NULL;
    while ((record = g_queue_pop_head(threadData->localRecordBundle)) != NULL) {
        logrecord_unref(record);
    }
    g_queue_free(threadData->localRecordBundle);
    g_async_queue_unref(threadData->remoteLogHelperMailbox);

    g_timer_destroy(threadData->runTimer);

    MAGIC_CLEAR(threadData);
    g_free(threadData);
}

void shadow_logger_setDefault(ShadowLogger* logger) {
    if (logger != NULL) {
        MAGIC_ASSERT(logger);
        shadow_logger_ref(logger);
    }
    logger_setDefault((Logger*)logger);
}

ShadowLogger* shadow_logger_getDefault() {
    return (ShadowLogger*)logger_getDefault();
}

void shadow_logger_setFilterLevel(ShadowLogger* logger, LogLevel level) {
    MAGIC_ASSERT(logger);
    logger->filterLevel = level;
}

gboolean shadow_logger_shouldFilter(ShadowLogger* logger, LogLevel level) {
    MAGIC_ASSERT(logger);

    /* check if the message should be filtered */
    LogLevel nodeLevel = LOGLEVEL_UNSET;

    /* if we have a host, its log level filter override default logger level
     * filter */
    if (worker_isAlive()) {
        Host* currentHost = worker_getActiveHost();
        if (currentHost != NULL) {
            nodeLevel = host_getLogLevel(currentHost);
        }
    }

    /* prefer the node filter level if we have one, fall back to default logger
     * filter */
    LogLevel filter =
        (nodeLevel != LOGLEVEL_UNSET) ? nodeLevel : logger->filterLevel;
    return (level > filter) ? TRUE : FALSE;
}

void shadow_logger_setEnableBuffering(ShadowLogger* logger, gboolean enabled) {
    MAGIC_ASSERT(logger);
    logger->shouldBuffer = enabled;
}

static void _logger_sendRegisterCommandToHelper(ShadowLogger* logger,
                                                LoggerThreadData* threadData) {
    LoggerHelperCommand* command = loggerhelpercommand_new(
        LHC_REGISTER, threadData->remoteLogHelperMailbox);
    g_async_queue_ref(threadData->remoteLogHelperMailbox);
    g_async_queue_push(logger->helperCommands, command);
}

static void _logger_sendFlushCommandToHelper(ShadowLogger* logger) {
    LoggerHelperCommand* command = loggerhelpercommand_new(LHC_FLUSH, NULL);
    g_async_queue_push(logger->helperCommands, command);
}

static void _logger_sendStopCommandToHelper(ShadowLogger* logger) {
    LoggerHelperCommand* command = loggerhelpercommand_new(LHC_STOP, NULL);
    g_async_queue_push(logger->helperCommands, command);
}

static void _logger_stopHelper(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);
    /* tell the logger helper that we are done sending commands */
    _logger_sendStopCommandToHelper(logger);

    /* wait until the thread exits.
     * XXX: calling thread_join may cause deadlocks in the loader, so let's just
     * wait for the thread to indicate that it finished everything instead. */
    // pthread_join(logger->helper);
    countdownlatch_await(logger->helperLatch);
}

void shadow_logger_logVA(ShadowLogger* logger, LogLevel level,
                         const gchar* fileName, const gchar* functionName,
                         const gint lineNumber, const gchar* format,
                         va_list vargs) {
    if (!logger) {
        vfprintf(stderr, format, vargs);
        return;
    }

    MAGIC_ASSERT(logger);

    if (shadow_logger_shouldFilter(logger, level)) {
        return;
    }

    LoggerThreadData* threadData = g_hash_table_lookup(
        logger->threadToDataMap, GUINT_TO_POINTER(pthread_self()));
    MAGIC_ASSERT(threadData);

    gdouble timespan = g_timer_elapsed(threadData->runTimer, NULL);

    LogRecord* record =
        logrecord_new(level, timespan, fileName, functionName, lineNumber);
    logrecord_formatMessageVA(record, format, vargs);

    if (worker_isAlive()) {
        /* time info */
        logrecord_setTime(record, worker_getCurrentTime());

        /* name info for the host */
        GString* hostNameBuffer = g_string_new("n/a");
        Host* activeHost = worker_getActiveHost();
        if (activeHost) {
            Address* hostAddress = host_getDefaultAddress(activeHost);
            if (hostAddress) {
                g_string_printf(hostNameBuffer, "%s~%s",
                                host_getName(activeHost),
                                address_toHostIPString(hostAddress));
            }
        }

        /* name info for the thread */
        GString* threadNameBuffer = g_string_new(NULL);
        g_string_printf(threadNameBuffer, "thread-%i", worker_getThreadID());

        /* set and cleanup */
        logrecord_setNames(record, threadNameBuffer->str, hostNameBuffer->str);
        g_string_free(threadNameBuffer, TRUE);
        g_string_free(hostNameBuffer, TRUE);
    }

    g_queue_push_tail(threadData->localRecordBundle, record);

    if (level == LOGLEVEL_ERROR || !logger->shouldBuffer ||
        (timespan - logger->lastTimespan) >= 5) {
        /* make sure we have logged everything */
        shadow_logger_flushRecords(logger, pthread_self());
        shadow_logger_syncToDisk(logger);
        logger->lastTimespan = timespan;
    }

    if (level == LOGLEVEL_ERROR) {
        /* tell the helper to stop, and join to make sure it finished flushing
         */
        _logger_stopHelper(logger);

        /* now abort, but get a backtrace */
        utility_assert(FALSE && "failure due to error-level log message");
    }
}

void shadow_logger_log(ShadowLogger* logger, LogLevel level,
                       const gchar* fileName, const gchar* functionName,
                       const gint lineNumber, const gchar* format, ...) {
    va_list vargs;
    va_start(vargs, format);
    shadow_logger_logVA(logger, level, fileName, functionName, lineNumber,
                        format, vargs);
    va_end(vargs);
}

void shadow_logger_register(ShadowLogger* logger, pthread_t callerThread) {
    MAGIC_ASSERT(logger);

    /* this must be called by main thread before the workers start accessing the
     * logger! */

    if (g_hash_table_lookup(logger->threadToDataMap,
                            GUINT_TO_POINTER(callerThread)) == NULL) {
        LoggerThreadData* threadData = _loggerthreaddata_new(logger->runTimer);
        g_hash_table_replace(logger->threadToDataMap,
                             GUINT_TO_POINTER(callerThread), threadData);
        _logger_sendRegisterCommandToHelper(logger, threadData);
    }
}

void shadow_logger_syncToDisk(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);
    _logger_sendFlushCommandToHelper(logger);
}

void shadow_logger_flushRecords(ShadowLogger* logger, pthread_t callerThread) {
    MAGIC_ASSERT(logger);
    LoggerThreadData* threadData = g_hash_table_lookup(
        logger->threadToDataMap, GUINT_TO_POINTER(callerThread));
    MAGIC_ASSERT(threadData);
    /* send log messages from this thread to the helper */
    if (!g_queue_is_empty(threadData->localRecordBundle)) {
        g_async_queue_push(threadData->remoteLogHelperMailbox,
                           threadData->localRecordBundle);
        threadData->localRecordBundle = g_queue_new();
    }
}

static gchar* _logger_getNewLocalTimeStr(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);

    GDateTime* nowDateTime = g_date_time_new_now_local();

    gchar* nowStr = nowDateTime ? g_date_time_format(nowDateTime, "%F %H:%M:%S")
                                : strdup("0000-00-00 00:00:00");

    if (nowDateTime) {
        g_date_time_unref(nowDateTime);
    }

    return nowStr;
}

static gchar* _logger_getNewRunTimeStr(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);

    /* compute our run time */
    guint64 elapsed = g_timer_elapsed(logger->runTimer, NULL);
    guint64 hours = elapsed / 3600;
    elapsed %= 3600;
    guint64 minutes = elapsed / 60;
    elapsed %= 60;
    guint64 seconds = elapsed;

    /* create a buffer to hold the string */
    GString* runTimeString = g_string_new(NULL);
    g_string_printf(runTimeString,
                    "%02" G_GUINT64_FORMAT ":%02" G_GUINT64_FORMAT
                    ":%02" G_GUINT64_FORMAT,
                    hours, minutes, seconds);

    /* free the gstring and return the str */
    return g_string_free(runTimeString, FALSE);
}

static void _logger_logStartupMessage(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);

    gchar* nowStr = _logger_getNewLocalTimeStr(logger);

    shadow_logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__,
                      __LINE__, "logging system started at %s", nowStr);

    if (nowStr) {
        g_free(nowStr);
    }
}

static void _logger_logShutdownMessage(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);

    gchar* nowStr = _logger_getNewLocalTimeStr(logger);
    gchar* runTimeStr = _logger_getNewRunTimeStr(logger);

    shadow_logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__,
                      __LINE__, "logging system stopped at %s, run time was %s",
                      nowStr, runTimeStr);

    if (nowStr) {
        g_free(nowStr);
    }
    if (runTimeStr) {
        g_free(runTimeStr);
    }
}

static void _shadow_logger_log_cb(Logger* logger, LogLevel level,
                                  const gchar* fileName,
                                  const gchar* functionName,
                                  const gint lineNumber, const gchar* format,
                                  va_list vargs) {
    shadow_logger_logVA((ShadowLogger*)logger, level, fileName, functionName,
                        lineNumber, format, vargs);
}

static void _shadow_logger_destroy_cb(Logger* logger) {
    shadow_logger_unref((ShadowLogger*)logger);
}

ShadowLogger* shadow_logger_new(LogLevel filterLevel) {
    ShadowLogger* logger = g_new(ShadowLogger, 1);
    *logger = (ShadowLogger){
        .base =
            {
                .log = _shadow_logger_log_cb,
                .destroy = _shadow_logger_destroy_cb,
            },
        .runTimer = g_timer_new(),
        .filterLevel = filterLevel,
        .shouldBuffer = TRUE,
        .referenceCount = 1,
        .threadToDataMap =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                                  (GDestroyNotify)_loggerthreaddata_free),

        .helperCommands = g_async_queue_new(),
        .helperLatch = countdownlatch_new(1),
    };
    MAGIC_INIT(logger);

    /* we need to pass some args to the helper thread */
    LoggerHelperRunData* runArgs = g_new0(LoggerHelperRunData, 1);
    runArgs->commands = logger->helperCommands;
    runArgs->notifyDoneRunning = logger->helperLatch;

    /* the thread will consume the reference to the runArgs struct, and will
     * free it */
    gint returnVal =
        pthread_create(&(logger->helper), NULL,
                       (void* (*)(void*))loggerhelper_runHelperThread, runArgs);
    if (returnVal != 0) {
        return NULL;
    }

    pthread_setname_np(logger->helper, "logger-helper");

    shadow_logger_register(logger, pthread_self());

    _logger_logStartupMessage(logger);

    return logger;
}

static void _logger_free(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);

    /* print the final log message that we are shutting down
     * this will be the last message printed by our logger */
    _logger_logShutdownMessage(logger);

    /* one last flush for the above message before we stop */
    shadow_logger_flushRecords(logger, pthread_self());
    shadow_logger_syncToDisk(logger);

    /* tell the helper to stop, waiting for it to stop */
    _logger_stopHelper(logger);

    /* all commands should have been handled and we can free the queue */
    utility_assert(g_async_queue_length_unlocked(logger->helperCommands) == 0);
    g_async_queue_unref(logger->helperCommands);
    countdownlatch_free(logger->helperLatch);

    g_hash_table_destroy(logger->threadToDataMap);
    g_timer_destroy(logger->runTimer);

    MAGIC_CLEAR(logger);
    g_free(logger);
}

void shadow_logger_ref(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);
    logger->referenceCount++;
}

void shadow_logger_unref(ShadowLogger* logger) {
    MAGIC_ASSERT(logger);
    logger->referenceCount--;
    gboolean shouldFree = (logger->referenceCount <= 0) ? TRUE : FALSE;
    if (shouldFree) {
        _logger_free(logger);
    }
}
