/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

static const gchar* _logging_getNewLogLevelString(GLogLevelFlags log_level) {
    switch (log_level) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

/* this func is called whenever g_logv is called, not just in our log code */
void logging_handleLog(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
    /* GLogLevelFlags* configuredLogLevel = user_data; */
    const gchar* logDomainStr = log_domain ? log_domain : "shadow";
    const gchar* messageStr = message ? message : "n/a";

    /* check again if the message should be filtered */
    if(worker_isFiltered(log_level)) {
        return;
    }

    gulong hours = 0, minutes = 0, seconds = 0, microseconds = 0;
    gulong elapsed = 0;
    if(worker_isAlive()) {
        elapsed = (gulong) g_timer_elapsed(worker_getRunTimer(), &microseconds);
        hours = elapsed / 3600;
        elapsed %= 3600;
        minutes = elapsed / 60;
        seconds = elapsed % 60;
    }

    g_print("%02lu:%02lu:%02lu.%06lu %s\n", hours, minutes, seconds, microseconds, messageStr);

    if(log_level & G_LOG_LEVEL_ERROR) {
        /* error level logs always abort, but glibs messages are not that useful.
         * lets override that with our own debug info and preemtively abort */
        utility_assert(FALSE && "failure due to error-level log message");
    }
}

void logging_logv(const gchar *msgLogDomain, GLogLevelFlags msgLogLevel,
        const gchar* fileName, const gchar* functionName, const gint lineNumber,
        const gchar *format, va_list vargs) {
    /* this is called by worker threads, so we have access to worker */

    /* see if we can avoid some work because the message is filtered anyway */
    const gchar* logDomainStr = msgLogDomain ? msgLogDomain : "shadow";
    if(worker_isFiltered(msgLogLevel)) {
        return;
    }

    gchar* logFileStr = fileName ? g_path_get_basename(fileName) : g_strdup("n/a");
    const gchar* logFunctionStr = functionName ? functionName : "n/a";
    const gchar* formatStr = format ? format : "n/a";
    const gchar* logLevelStr = _logging_getNewLogLevelString(msgLogLevel);

    SimulationTime currentTime = worker_isAlive() ? worker_getCurrentTime() : SIMTIME_INVALID;
    Host* currentHost = worker_isAlive() ? worker_getCurrentHost() : NULL;
    gint workerThreadID = worker_isAlive() ? worker_getThreadID() : 0;

    /* format the simulation time if we are running an event */
    GString* clockStringBuffer = g_string_new("");
    if(currentTime != SIMTIME_INVALID) {
        SimulationTime hours, minutes, seconds, remainder;
        remainder = currentTime;

        hours = remainder / SIMTIME_ONE_HOUR;
        remainder %= SIMTIME_ONE_HOUR;
        minutes = remainder / SIMTIME_ONE_MINUTE;
        remainder %= SIMTIME_ONE_MINUTE;
        seconds = remainder / SIMTIME_ONE_SECOND;
        remainder %= SIMTIME_ONE_SECOND;

        g_string_printf(clockStringBuffer, "%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT":%02"G_GUINT64_FORMAT".%09"G_GUINT64_FORMAT"",
                hours, minutes, seconds, remainder);
    } else {
        g_string_printf(clockStringBuffer, "n/a");
    }

    /* we'll need to free clockString later */
    gchar* clockString = g_string_free(clockStringBuffer, FALSE);

    /* node identifier, if we are running a node
     * dont free this since we dont own the ip address string */
    GString* nodeStringBuffer = g_string_new("");
    if(currentHost) {
        g_string_printf(nodeStringBuffer, "%s~%s", host_getName(currentHost), host_getDefaultIPName(currentHost));
    } else {
        g_string_printf(nodeStringBuffer, "n/a");
    }
    gchar* nodeString = g_string_free(nodeStringBuffer, FALSE);

    /* the function name - no need to free this */
    GString* newLogFormatBuffer = g_string_new(NULL);
    g_string_printf(newLogFormatBuffer, "[thread-%i] %s [%s-%s] [%s] [%s:%i] [%s] %s",
            workerThreadID, clockString, logDomainStr, logLevelStr, nodeString,
            logFileStr, lineNumber, logFunctionStr, formatStr);

    /* get the new format out of our string buffer and log it */
    gchar* newLogFormat = g_string_free(newLogFormatBuffer, FALSE);
    g_logv(logDomainStr, msgLogLevel, newLogFormat, vargs);

    /* cleanup */
    g_free(logFileStr);
    g_free(newLogFormat);
    g_free(clockString);
    g_free(nodeString);
}

void logging_log(const gchar *log_domain, GLogLevelFlags log_level,
        const gchar* fileName, const gchar* functionName, const gint lineNumber,
        const gchar *format, ...) {
    va_list vargs;
    va_start(vargs, format);

    logging_logv(log_domain, log_level, fileName, functionName, lineNumber, format, vargs);

    va_end(vargs);
}
