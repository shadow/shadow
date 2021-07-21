#include "lib/logger/logger.h"

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
    int64_t unaccounted_micros = logger_elapsed_micros();

    int hours = unaccounted_micros / (3600LL * G_USEC_PER_SEC);
    unaccounted_micros %= (3600LL * G_USEC_PER_SEC);
    int minutes = unaccounted_micros / (60 * G_USEC_PER_SEC);
    unaccounted_micros %= (60 * G_USEC_PER_SEC);
    int secs = unaccounted_micros / G_USEC_PER_SEC;
    unaccounted_micros %= G_USEC_PER_SEC;
    int micros = unaccounted_micros;

    return snprintf(dst, size, "%02d:%02d:%02d.%06d", hours, minutes, secs, micros);
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

void _logger_default_flush() {
    fflush(stderr);
}

static void _logger_default_log(LogLevel level, const char* fileName, const char* functionName,
                                const int lineNumber, const char* format, va_list vargs) {
    static __thread bool in_logger = false;
    if (in_logger) {
        // Avoid recursing. We do this here rather than in logger_log so that
        // specialized loggers could potentially do something better than just
        // dropping the message.
        return;
    }
    if (!logger_isEnabled(NULL, level)) {
        return;
    }
    in_logger = true;

    // Stack-allocated to avoid dynamic allocation.
    char buf[200];
    size_t offset = 0;

    // Keep appending to string. These functions all ensure NULL-byte termination.
    offset += logger_elapsed_string(&buf[offset], sizeof(buf) - offset);
    offset = MIN(offset, sizeof(buf));

    offset += snprintf(&buf[offset], sizeof(buf) - offset, " %s [%s:%i] [%s] ",
                       loglevel_toStr(level), logger_base_name(fileName), lineNumber, functionName);
    offset = MIN(offset, sizeof(buf));

    offset += vsnprintf(&buf[offset], sizeof(buf) - offset, format, vargs);
    offset = MIN(offset, sizeof(buf));

    offset += fprintf(stderr, "%s\n", buf);
    offset = MIN(offset, sizeof(buf));

        _logger_default_flush();
    in_logger = false;
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
        logger_flush(logger);
    }
}

void logger_setLevel(Logger* logger, LogLevel level) {
    if (!logger) {
        // Not implemented for default logger.
    } else {
        logger->setLevel(logger, level);
    }
}

bool logger_isEnabled(Logger* logger, LogLevel level) {
    if (!logger) {
        // Most logging frameworks log little/nothing unless explicitly enabled.
        // That probably makes sense for a framework used across independent
        // libraries and apps, but in our case verbose is a useful default,
        // particularly in test programs.
        return true;
    } else {
        return logger->isEnabled(logger, level);
    }
}

void logger_flush(Logger* logger) {
    if (!logger) {
        _logger_default_flush();
    } else {
        logger->flush(logger);
    }
}
