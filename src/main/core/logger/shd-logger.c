/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <pthread.h>
#include "shadow.h"

/* this stores thread-specific data for each "worker" thread (the threads that
 * are running the virtual nodes) */
typedef struct _LoggerThreadData LoggerThreadData;
struct _LoggerThreadData {
    /* keep wall time without relying on main logger data */
    GTimer* runTimer;
    gdouble loggerRunOffset;

    /* local temporary store for this threads log records */
    GQueue* localRecordBundle;

    /* remote queue over which to send helper thread messages */
    GAsyncQueue* remoteLogHelperMailbox;
    MAGIC_DECLARE;
};

/* manages the logging of messages among multiple worker threads */
struct _Logger {
    GTimer* runTimer;

    /* the level below which we filter messages */
    LogLevel filterLevel;

    /* helper to sort messages and handle file i/o */
    GThread* helper;
    GAsyncQueue* helperCommands;

    /* store map of other threads that will call logging functions to thread-specific data */
    GHashTable* threadToDataMap;

    /* for memory management */
    gint referenceCount;
    MAGIC_DECLARE;
};

static Logger* defaultLogger = NULL;

static LoggerThreadData* _loggerthreaddata_new(GTimer* loggerTimer) {
    LoggerThreadData* threadData = g_new0(LoggerThreadData, 1);
    MAGIC_INIT(threadData);

    threadData->runTimer = g_timer_new();
    threadData->loggerRunOffset = g_timer_elapsed(loggerTimer, NULL);

    threadData->localRecordBundle = g_queue_new();
    threadData->remoteLogHelperMailbox = g_async_queue_new();

    return threadData;
}

static void _loggerthreaddata_free(LoggerThreadData* threadData) {
    MAGIC_ASSERT(threadData);

    /* free any remaining log records and the record queue */
    LogRecord* record = NULL;
    while((record = g_queue_pop_head(threadData->localRecordBundle)) != NULL) {
        logrecord_unref(record);
    }
    g_queue_free(threadData->localRecordBundle);
    g_async_queue_unref(threadData->remoteLogHelperMailbox);

    g_timer_destroy(threadData->runTimer);

    MAGIC_CLEAR(threadData);
    g_free(threadData);
}

void logger_setDefault(Logger* logger) {
    if(defaultLogger != NULL) {
        logger_unref(defaultLogger);
        defaultLogger = NULL;
    }
    if(logger != NULL) {
        MAGIC_ASSERT(logger);
        defaultLogger = logger;
        logger_ref(logger);
    }
}

Logger* logger_getDefault() {
    /* may return NULL */
    return defaultLogger;
}

void logger_setFilterLevel(Logger* logger, LogLevel level) {
    MAGIC_ASSERT(logger);
    logger->filterLevel = level;
}

gboolean logger_shouldFilter(Logger* logger, LogLevel level) {
    MAGIC_ASSERT(logger);

    /* check if the message should be filtered */
    LogLevel nodeLevel = LOGLEVEL_UNSET;

    /* if we have a host, its log level filter override default logger level filter */
    if(worker_isAlive()) {
        Host* currentHost = worker_getActiveHost();
        if(currentHost != NULL) {
            nodeLevel = host_getLogLevel(currentHost);
        }
    }

    /* prefer the node filter level if we have one, fall back to default logger filter */
    LogLevel filter = (nodeLevel != LOGLEVEL_UNSET) ? nodeLevel : logger->filterLevel;
    return (level > filter) ? TRUE : FALSE;
}

static void _logger_sendRegisterCommandToHelper(Logger* logger, LoggerThreadData* threadData) {
    LoggerHelperCommand* command = loggerhelpercommand_new(LHC_REGISTER, threadData->remoteLogHelperMailbox);
    g_async_queue_ref(threadData->remoteLogHelperMailbox);
    g_async_queue_push(logger->helperCommands, command);
}

static void _logger_sendFlushCommandToHelper(Logger* logger) {
    LoggerHelperCommand* command = loggerhelpercommand_new(LHC_FLUSH, NULL);
    g_async_queue_push(logger->helperCommands, command);
}

static void _logger_sendStopCommandToHelper(Logger* logger) {
    LoggerHelperCommand* command = loggerhelpercommand_new(LHC_STOP, NULL);
    g_async_queue_push(logger->helperCommands, command);
}

void logger_logVA(Logger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, va_list vargs) {
    MAGIC_ASSERT(logger);

    if(logger_shouldFilter(logger, level)) {
        return;
    }

    LoggerThreadData* threadData = g_hash_table_lookup(logger->threadToDataMap, g_thread_self());
    MAGIC_ASSERT(threadData);

    gdouble timespan = g_timer_elapsed(threadData->runTimer, NULL);

    LogRecord* record = logrecord_new(level, timespan, fileName, functionName, lineNumber);
    logrecord_formatMessageVA(record, format, vargs);

    if(worker_isAlive()) {
        /* time info */
        logrecord_setTime(record, worker_getCurrentTime());

        /* name info for the host */
        GString* hostNameBuffer = g_string_new("n/a");
        Host* activeHost = worker_getActiveHost();
        if(activeHost) {
            g_string_printf(hostNameBuffer, "%s~%s", host_getName(activeHost), host_getDefaultIPName(activeHost));
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

    if(level == LOGLEVEL_ERROR) {
        /* error log level will abort, make sure we have logged everything first */
        logger_flushRecords(logger, g_thread_self());
        logger_syncToDisk(logger);

        /* now abort, but get a backtrace */
        utility_assert(FALSE && "failure due to error-level log message");
    }
}

void logger_log(Logger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, ...) {
    MAGIC_ASSERT(logger);
    va_list vargs;
    va_start(vargs, format);
    logger_logVA(logger, level, fileName, functionName, lineNumber, format, vargs);
    va_end(vargs);
}

void logger_register(Logger* logger, GThread* callerThread) {
    MAGIC_ASSERT(logger);

    /* this must be called by main thread before the workers start accessing the logger! */

    if(g_hash_table_lookup(logger->threadToDataMap, callerThread) == NULL) {
        LoggerThreadData* threadData = _loggerthreaddata_new(logger->runTimer);
        g_hash_table_replace(logger->threadToDataMap, callerThread, threadData);
        _logger_sendRegisterCommandToHelper(logger, threadData);
    }
}

void logger_syncToDisk(Logger* logger) {
    MAGIC_ASSERT(logger);
    _logger_sendFlushCommandToHelper(logger);
}

void logger_flushRecords(Logger* logger, GThread* callerThread) {
    MAGIC_ASSERT(logger);
    LoggerThreadData* threadData = g_hash_table_lookup(logger->threadToDataMap, callerThread);
    MAGIC_ASSERT(threadData);
    /* send log messages from this thread to the helper */
    g_async_queue_push(threadData->remoteLogHelperMailbox, threadData->localRecordBundle);
    threadData->localRecordBundle = g_queue_new();
}

Logger* logger_new(LogLevel filterLevel) {
    Logger* logger = g_new0(Logger, 1);
    MAGIC_INIT(logger);

    logger->runTimer = g_timer_new();
    logger->filterLevel = filterLevel;
    logger->referenceCount = 1;
    logger->threadToDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_loggerthreaddata_free);

    logger->helperCommands = g_async_queue_new();
    logger->helper = g_thread_new("logger-helper", (GThreadFunc)loggerhelper_runHelperThread, logger->helperCommands);

    logger_register(logger, g_thread_self());

    GDateTime* nowDateTime = g_date_time_new_now_local();
    gchar* nowStr = g_date_time_format(nowDateTime, "%F %H:%M:%S");
    logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__, __LINE__, "logging system started at %s", nowStr);
    g_date_time_unref(nowDateTime);
    g_free(nowStr);

    return logger;
}

static void _logger_free(Logger* logger) {
    MAGIC_ASSERT(logger);

    /* compute our run time */
    guint64 elapsed = g_timer_elapsed(logger->runTimer, NULL);
    guint64 hours = elapsed/3600;
    elapsed %= 3600;
    guint64 minutes = elapsed/60;
    elapsed %= 60;
    guint64 seconds = elapsed;

    /* print the final log message that we are shutting down */
    GDateTime* nowDateTime = g_date_time_new_now_local();
    gchar* nowStr = g_date_time_format(nowDateTime, "%F %H:%M:%S");
    logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__, __LINE__,
            "logging system stopped at %s, run time was "
            "%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT,
            nowStr, hours, minutes, seconds);
    g_date_time_unref(nowDateTime);
    g_free(nowStr);

    /* one last flush for the above message before we stop */
    logger_flushRecords(logger, g_thread_self());
    logger_syncToDisk(logger);

    /* tell the helper to stop, wait for it to stop, and then free it */
    _logger_sendStopCommandToHelper(logger);
    g_thread_join(logger->helper);
    g_thread_unref(logger->helper);

    /* all commands should have been handled and we can free teh queue */
    utility_assert(g_async_queue_length_unlocked(logger->helperCommands) == 0);
    g_async_queue_unref(logger->helperCommands);

    g_hash_table_destroy(logger->threadToDataMap);
    g_timer_destroy(logger->runTimer);

    MAGIC_CLEAR(logger);
    g_free(logger);
}

void logger_ref(Logger* logger) {
    MAGIC_ASSERT(logger);
    logger->referenceCount++;
}

void logger_unref(Logger* logger) {
    MAGIC_ASSERT(logger);
    logger->referenceCount--;
    gboolean shouldFree = (logger->referenceCount <= 0) ? TRUE : FALSE;
    if(shouldFree) {
        _logger_free(logger);
    }
}
