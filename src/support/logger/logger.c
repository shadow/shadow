#include "support/logger/logger.h"

#include <glib.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
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

size_t logger_elapsed_string(char* dst, size_t size) {
    int64_t elapsed_micros = logger_elapsed_micros();
    struct timeval tv = {
        .tv_sec = elapsed_micros / G_USEC_PER_SEC,
        .tv_usec = elapsed_micros % G_USEC_PER_SEC,
    };
    struct tm tm;
    gmtime_r(&tv.tv_sec, &tm);
    return snprintf(
        dst, size, "%02d:%02d:%02d.%06ld", tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
}

// Returns a pointer into `filename`, after all directories. Doesn't strip a final path separator.
//
// bar       -> bar
// foo/bar   -> bar
// /foo/bar  -> bar
// /foo/bar/ -> bar/
const char* logger_base_name(const char* filename) {
    const char* rv = filename;
    for (const char* pos = filename; *pos != '\0'; ++pos) {
        if (*pos == '/' && *(pos + 1) != '\0') {
            rv = pos + 1;
        }
    }
    return rv;
}

static void _logger_default_log(LogLevel level, const char* fileName, const char* functionName,
                                const int lineNumber, const char* format, va_list vargs) {
    // Stack-allocated to avoid dynamic allocation.
    char buf[200];
    size_t offset = 0;

    // Keep appending to string. These functions all ensure NULL-byte termination.
    offset += logger_elapsed_string(&buf[offset], sizeof(buf) - offset);
    offset = MIN(offset, sizeof(buf));

    offset += snprintf(&buf[offset], sizeof(buf) - offset, "%s [%s:%i] [%s] ",
                       loglevel_toStr(level), logger_base_name(fileName), lineNumber, functionName);
    offset = MIN(offset, sizeof(buf));

    offset += vsnprintf(&buf[offset], sizeof(buf) - offset, format, vargs);
    offset = MIN(offset, sizeof(buf));

    offset += printf("%s\n", buf);
    offset = MIN(offset, sizeof(buf));

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
