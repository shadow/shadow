#include "support/logger/logger.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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
static pthread_once_t _start_time_once = PTHREAD_ONCE_INIT;
static bool _start_time_initd = false;
static int64_t _monotonic_start_time_micros;
static void _init_start_time() {
    if (_start_time_initd) {
        // Was already initialized explicitly using
        // logger_set_global_start_time_micros.
        return;
    }
    _start_time_initd = true;
    _monotonic_start_time_micros = logger_now_micros();
}

int64_t logger_now_micros() {
    return g_get_monotonic_time();
}

int64_t logger_get_global_start_time_micros() {
    pthread_once(&_start_time_once, _init_start_time);
    int64_t t = _monotonic_start_time_micros;
    return t;
}

void logger_set_global_start_time_micros(int64_t t) {
    _monotonic_start_time_micros = t;
    _start_time_initd = true;
}

int64_t logger_elapsed_micros() {
    // We need to be careful here to get t0 first, since the first time this
    // function is called it will cause the start time to be lazily initialized.
    int64_t t0 = logger_get_global_start_time_micros();
    return logger_now_micros() - t0;
}

static void _logger_default_log(LogLevel level, const gchar* fileName,
                                const gchar* functionName,
                                const gint lineNumber, const gchar* format,
                                va_list vargs) {
    gchar* message = g_strdup_vprintf(format, vargs);
    gchar* baseName = g_path_get_basename(fileName);

    int64_t elapsed_micros = logger_elapsed_micros();
    struct timeval tv = {
        .tv_sec = elapsed_micros / G_USEC_PER_SEC,
        .tv_usec = elapsed_micros % G_USEC_PER_SEC,
    };
    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);
    gchar* timeString = g_strdup_printf("%02d:%02d:%02d.%06ld", tm.tm_hour,
                                        tm.tm_min, tm.tm_sec, tv.tv_usec);
    g_print("%s %s [%s:%i] [%s] %s\n", timeString, loglevel_toStr(level),
            baseName, lineNumber, functionName, message);
    g_free(message);
    g_free(timeString);
    g_free(baseName);
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
    if (level == LOGLEVEL_ERROR) {
#ifdef DEBUG
        // Dumps a core file (if the system is configured to do so), but may not
        // clean up properly. e.g. `atexit` handlers won't be run.
        abort();
#else
        exit(1);
#endif
    }
}
