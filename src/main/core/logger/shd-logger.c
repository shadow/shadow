/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#include "shadow.h"

/* manages the logging of messages among multiple threads */
struct _Logger {
    GTimer* runTimer;

    /* the level below which we filter messages */
    LogLevel filterLevel;

    /* global lock for all threads, hold this as little as possible */
    GMutex globalLock;

    /* for memory management */
    gint referenceCount;
    MAGIC_DECLARE;
};

static Logger* defaultLogger = NULL;

Logger* logger_new(LogLevel filterLevel) {
    Logger* logger = g_new0(Logger, 1);
    MAGIC_INIT(logger);

    g_mutex_init(&(logger->globalLock));

    logger->runTimer = g_timer_new();
    logger->filterLevel = filterLevel;
    logger->referenceCount = 1;

    GDateTime* nowDateTime = g_date_time_new_now_local();
    gchar* nowStr = g_date_time_format(nowDateTime, "%F %H:%M:%S");
    logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__, __LINE__, "logging system started at %s", nowStr);
    g_date_time_unref(nowDateTime);
    g_free(nowStr);

    return logger;
}

static void _logger_free(Logger* logger) {
    MAGIC_ASSERT(logger);

    guint64 elapsed = g_timer_elapsed(logger->runTimer, NULL);
    guint64 hours = elapsed/3600;
    elapsed %= 3600;
    guint64 minutes = elapsed/60;
    elapsed %= 60;
    guint64 seconds = elapsed;

    GDateTime* nowDateTime = g_date_time_new_now_local();
    gchar* nowStr = g_date_time_format(nowDateTime, "%F %H:%M:%S");
    logger_log(logger, LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__, __LINE__,
            "logging system stopped at %s, run time was "
            "%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT,
            nowStr, hours, minutes, seconds);
    g_date_time_unref(nowDateTime);
    g_free(nowStr);

    g_timer_destroy(logger->runTimer);
    g_mutex_clear(&(logger->globalLock));

    MAGIC_CLEAR(logger);
    g_free(logger);
}

void logger_ref(Logger* logger) {
    MAGIC_ASSERT(logger);

    g_mutex_lock(&(logger->globalLock));

    logger->referenceCount++;

    g_mutex_unlock(&(logger->globalLock));
}

void logger_unref(Logger* logger) {
    MAGIC_ASSERT(logger);

    g_mutex_lock(&(logger->globalLock));

    logger->referenceCount--;
    gboolean shouldFree = (logger->referenceCount <= 0) ? TRUE : FALSE;

    g_mutex_unlock(&(logger->globalLock));

    if(shouldFree) {
        _logger_free(logger);
    }
}

void logger_setDefault(Logger* logger) {
    MAGIC_ASSERT(logger);
    if(defaultLogger != NULL) {
        logger_unref(defaultLogger);
    }
    defaultLogger = logger;
    logger_ref(logger);
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

void logger_logVA(Logger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, va_list vargs) {
    MAGIC_ASSERT(logger);

    if(logger_shouldFilter(logger, level)) {
        return;
    }

    gdouble timespan = g_timer_elapsed(logger->runTimer, NULL);

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

    gchar* logRecordStr = logrecord_toString(record);
    utility_assert(logRecordStr);

    g_mutex_lock(&logger->globalLock);
    g_print("%s", logRecordStr);
    g_mutex_unlock(&logger->globalLock);

    g_free(logRecordStr);

    if(level == LOGLEVEL_ERROR) {
        /* error level logs always abort, but glibs messages are not that useful.
         * lets override that with our own debug info and preemtively abort */
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
