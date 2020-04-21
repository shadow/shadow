/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "core/logger/log_record.h"

#include <stddef.h>

#include "utility/utility.h"

struct _LogRecord {
    /* required */
    LogLevel level;
    gchar* callInfo;

    /* optional */
    SimulationTime simElapsedNanos;
    gdouble wallElapsedSeconds;
    gchar* threadName;
    gchar* hostName;
    gchar* message;

    /* for memory management */
    gint referenceCount;
    MAGIC_DECLARE;
};

LogRecord* logrecord_new(LogLevel level, gdouble timespan, const gchar* fileName, const gchar* functionName, const gint lineNumber) {
    LogRecord* record = g_new0(LogRecord, 1);
    MAGIC_INIT(record);

    record->level = level;
    record->simElapsedNanos = SIMTIME_INVALID;
    record->wallElapsedSeconds = timespan;

    gchar* baseName = (fileName != NULL) ? g_path_get_basename(fileName) : NULL;

    record->callInfo = g_strdup_printf("[%s:%i] [%s]",
            (baseName != NULL) ? baseName : "n/a", lineNumber, functionName ? functionName : "n/a");

    if(baseName != NULL) {
        g_free(baseName);
    }

    return record;
}

static void _logrecord_free(LogRecord* record) {
    MAGIC_ASSERT(record);

    if(record->callInfo != NULL) {
        g_free(record->callInfo);
    }
    if(record->hostName != NULL) {
        g_free(record->hostName);
    }
    if(record->threadName != NULL) {
        g_free(record->threadName);
    }
    if(record->message != NULL) {
        g_free(record->message);
    }

    MAGIC_CLEAR(record);
    g_free(record);
}

void logrecord_ref(LogRecord* record) {
    MAGIC_ASSERT(record);
    record->referenceCount++;
}

void logrecord_unref(LogRecord* record) {
    MAGIC_ASSERT(record);
    record->referenceCount--;
    if(record->referenceCount <= 0) {
        _logrecord_free(record);
    }
}

gint logrecord_compare(const LogRecord* a, const LogRecord* b, gpointer userData) {
    MAGIC_ASSERT(a);
    MAGIC_ASSERT(b);
    return a->wallElapsedSeconds < b->wallElapsedSeconds ? -1 : a->wallElapsedSeconds > b->wallElapsedSeconds ? 1 : 0;
}

void logrecord_setTime(LogRecord* record, SimulationTime simElapsedNanos) {
    MAGIC_ASSERT(record);
    record->simElapsedNanos = simElapsedNanos;
}

void logrecord_setNames(LogRecord* record, const gchar* threadName, const gchar* hostName) {
    MAGIC_ASSERT(record);

    /* free the old ones if they exist */
    if(record->hostName != NULL) {
        g_free(record->hostName);
        record->hostName = NULL;
    }
    if(record->threadName != NULL) {
        g_free(record->threadName);
        record->threadName = NULL;
    }

    /* save the new ones */
    if(hostName != NULL) {
        record->hostName = g_strdup(hostName);
    }
    if(threadName != NULL) {
        record->threadName = g_strdup(threadName);
    }
}

void logrecord_formatMessageVA(LogRecord* record, const gchar *messageFormat, va_list vargs) {
    MAGIC_ASSERT(record);

    /* free the old one if it exists */
    if(record->message != NULL) {
        g_free(record->message);
        record->message = NULL;
    }

    if(messageFormat != NULL) {
        record->message = g_strdup_vprintf(messageFormat, vargs);
    }
}

void logrecord_formatMessage(LogRecord* record, const gchar *messageFormat, ...) {
    MAGIC_ASSERT(record);
    va_list vargs;
    va_start(vargs, messageFormat);
    logrecord_formatMessageVA(record, messageFormat, vargs);
    va_end(vargs);
}

static gchar* _logrecord_getNewSimTimeStr(LogRecord* record) {
    MAGIC_ASSERT(record);

    SimulationTime remainder = record->simElapsedNanos;

    SimulationTime hours = remainder / SIMTIME_ONE_HOUR;
    remainder %= SIMTIME_ONE_HOUR;
    SimulationTime minutes = remainder / SIMTIME_ONE_MINUTE;
    remainder %= SIMTIME_ONE_MINUTE;
    SimulationTime seconds = remainder / SIMTIME_ONE_SECOND;
    remainder %= SIMTIME_ONE_SECOND;
    SimulationTime nanoseconds = remainder;

    return g_strdup_printf("%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT".%09"G_GUINT64_FORMAT,
            hours, minutes, seconds, nanoseconds);
}

static gchar* _logrecord_getNewWallTimeStr(LogRecord* record) {
    MAGIC_ASSERT(record);

    guint64 remainder = (guint64)record->wallElapsedSeconds;
    gdouble fraction = record->wallElapsedSeconds - ((gdouble)remainder);

    guint64 hours = remainder/3600;
    remainder %= 3600;
    guint64 minutes = remainder/60;
    remainder %= 60;
    guint64 seconds = remainder;
    guint64 microseconds = (guint64)(fraction * ((gdouble)1000000));

    return g_strdup_printf("%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT".%06"G_GUINT64_FORMAT,
            hours, minutes, seconds, microseconds);
}


gchar* logrecord_toString(LogRecord* record) {
    MAGIC_ASSERT(record);
    utility_assert(record->callInfo);

    gchar* wallTimeStr = _logrecord_getNewWallTimeStr(record);
    gchar* simTimeStr = (record->simElapsedNanos != SIMTIME_INVALID) ? _logrecord_getNewSimTimeStr(record) : NULL;

    gchar* recordStr = g_strdup_printf("%s [%s] %s [%s] [%s] %s %s\n",
            (wallTimeStr != NULL) ? wallTimeStr : "n/a",
            (record->threadName != NULL) ? record->threadName : "thread-0",
            (simTimeStr != NULL) ? simTimeStr : "n/a",
            loglevel_toStr(record->level),
            (record->hostName != NULL) ? record->hostName : "n/a",
            record->callInfo,
            (record->message != NULL) ? record->message : "NOMESSAGE");


    if(wallTimeStr != NULL) {
        g_free(wallTimeStr);
    }
    if(simTimeStr != NULL) {
        g_free(simTimeStr);
    }

    return recordStr;
}
