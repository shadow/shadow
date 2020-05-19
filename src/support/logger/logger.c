#include "support/logger/logger.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/time.h>

static Logger* defaultLogger = NULL;

void logger_setDefault(Logger* logger) {
    if (defaultLogger != NULL) {
        defaultLogger->destroy(defaultLogger);
    }
    defaultLogger = logger;
}

Logger* logger_getDefault() { return defaultLogger; }

// Process start time, initialized explicitly or on first use.
static pthread_mutex_t _start_time_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool _start_time_initd = false;
static struct timeval _start_time;

struct timeval logger_get_global_start_time() {
    pthread_mutex_lock(&_start_time_mutex);
    if (!_start_time_initd) {
        // TODO: Parse out of /proc/$$/stat instead to get the true process
        // start time.
        gettimeofday(&_start_time, NULL);
        _start_time_initd = true;
    }
    struct timeval start_time = _start_time;
    pthread_mutex_unlock(&_start_time_mutex);
    return start_time;
}

void logger_set_global_start_time(const struct timeval* t) {
    pthread_mutex_lock(&_start_time_mutex);
    _start_time = *t;
    _start_time_initd = true;
    pthread_mutex_unlock(&_start_time_mutex);
}

struct timeval logger_get_global_elapsed_time() {
    const struct timeval start_time = logger_get_global_start_time();
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timeval elapsed;
    timersub(&now, &start_time, &elapsed);
    return elapsed;
}

static void _logger_default_log(LogLevel level, const gchar* fileName,
                                const gchar* functionName,
                                const gint lineNumber, const gchar* format,
                                va_list vargs) {
    gchar* message = g_strdup_vprintf(format, vargs);
    gchar* baseName = g_path_get_basename(fileName);

    struct timeval tv = logger_get_global_elapsed_time();
    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);
    gchar* timeString = g_strdup_printf("%02d:%02d:%02d.%06ld", tm.tm_hour,
                                        tm.tm_min, tm.tm_sec, tv.tv_usec);
    g_print("%s %s [%s:%i] [%s] %s\n", timeString, loglevel_toStr(level),
            baseName, lineNumber, functionName, message);
    g_free(message);
    g_free(timeString);
    g_free(baseName);
#ifdef DEBUG
    if (level == LOGLEVEL_ERROR) {
        abort();
    }
#endif
}

void logger_log(Logger* logger, LogLevel level, const gchar* fileName,
                const gchar* functionName, const gint lineNumber,
                const gchar* format, ...) {
    va_list vargs;
    va_start(vargs, format);
    if (!logger) {
        _logger_default_log(level, fileName, functionName, lineNumber, format,
                            vargs);
    } else {
        logger->log(logger, level, fileName, functionName, lineNumber, format,
                    vargs);
    }
    va_end(vargs);
}
