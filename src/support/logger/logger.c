#include "support/logger/logger.h"

#include <pthread.h>
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

// Process start time, initialized on first use.
// TODO: Parse out of /proc/$$/stat instead to get the true process start time.
static pthread_once_t _start_time_init_once = PTHREAD_ONCE_INIT;
static struct timeval _start_time;
static void _init_start_time() { gettimeofday(&_start_time, NULL); }
static const struct timeval* _get_start_time() {
    pthread_once(&_start_time_init_once, _init_start_time);
    return &_start_time;
}

static struct timeval _elapsed() {
    const struct timeval* start_time = _get_start_time();
    struct timeval now;
    gettimeofday(&now, NULL);
    struct timeval elapsed;
    timersub(&now, start_time, &elapsed);
    return elapsed;
}

static void _logger_default_log(LogLevel level, const gchar* fileName,
                                const gchar* functionName,
                                const gint lineNumber, const gchar* format,
                                va_list vargs) {
    gchar* message = g_strdup_vprintf(format, vargs);
    gchar* baseName = g_path_get_basename(fileName);

    struct timeval tv = _elapsed();
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
