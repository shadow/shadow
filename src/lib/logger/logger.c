#include "lib/logger/logger.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <unistd.h>

#include "lib/linux-api/linux-api.h"

#define USEC_PER_SEC 1000000ULL

_Noreturn void logger_abort() {
    linux_kill(0, LINUX_SIGABRT);
    asm("ud2");
    // Convince compiler that we really don't return.
    while (1)
        ;
};

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
    linux_timespec res;
    linux_clock_gettime(CLOCK_REALTIME, &res);
    return (res.tv_sec * USEC_PER_SEC) + (res.tv_nsec / 1000);
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

    int hours = unaccounted_micros / (3600LL * USEC_PER_SEC);
    unaccounted_micros %= (3600LL * USEC_PER_SEC);
    int minutes = unaccounted_micros / (60 * USEC_PER_SEC);
    unaccounted_micros %= (60 * USEC_PER_SEC);
    int secs = unaccounted_micros / USEC_PER_SEC;
    unaccounted_micros %= USEC_PER_SEC;
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

typedef struct {
    Logger base;
    _Atomic LogLevel level;
} StderrLogger;

static void _stderrlogger_flush(Logger* logger) {
    fflush(stderr);
}

static void _stderrlogger_log(Logger* logger, LogLevel level, const char* fileName,
                         const char* functionName, const int lineNumber, const char* format,
                         va_list vargs) {
    static __thread bool in_logger = false;
    if (in_logger) {
        // Avoid recursing. We do this here rather than in logger_log so that
        // specialized loggers could potentially do something better than just
        // dropping the message.
        return;
    }
    if (!logger_isEnabled(logger, level)) {
        return;
    }
    in_logger = true;

    // Stack-allocated to avoid dynamic allocation.
    char buf[2000];
    size_t offset = 0;

    // Keep appending to string. These functions all ensure NULL-byte termination.
    offset += logger_elapsed_string(&buf[offset], sizeof(buf) - offset);
    offset = MIN(offset, sizeof(buf));

    offset += snprintf(&buf[offset], sizeof(buf) - offset - 1, " %s [%s:%i] [%s] ",
                       loglevel_toStr(level), logger_base_name(fileName), lineNumber, functionName);
    offset = MIN(offset, sizeof(buf));

    offset += vsnprintf(&buf[offset], sizeof(buf) - offset - 1, format, vargs);
    offset = MIN(offset, sizeof(buf));

    buf[offset++] = '\n';

    if (write(STDERR_FILENO, buf, offset) < 0) {
        logger_abort();
    }

    in_logger = false;
}

static void _stderrlogger_destroy(Logger* logger) {
    // Do nothing; Default logger is statically allocated.
}

static void _stderrlogger_setLevel(Logger* baseLogger, LogLevel level) {
    StderrLogger* logger = (StderrLogger*)baseLogger;
    logger->level = level;
}

static bool _stderrlogger_isEnabled(Logger* baseLogger, LogLevel level) {
    StderrLogger* logger = (StderrLogger*)baseLogger;
    return level <= logger->level;
}

static StderrLogger stderrLogger = {
    .base = {
        .destroy = _stderrlogger_destroy,
        .flush = _stderrlogger_flush,
        .isEnabled = _stderrlogger_isEnabled,
        .log = _stderrlogger_log,
        .setLevel = _stderrlogger_setLevel,
    },
    .level = LOGLEVEL_TRACE,
};

static Logger* defaultLogger = &stderrLogger.base;

void logger_setDefault(Logger* logger) {
    if (defaultLogger != NULL) {
        defaultLogger->destroy(defaultLogger);
    }
    defaultLogger = logger;
}

Logger* logger_getDefault() { return defaultLogger; }

void logger_log(Logger* logger, LogLevel level, const char* fileName, const char* functionName,
                const int lineNumber, const char* format, ...) {
    if (!logger) {
        return;
    }
    va_list vargs;
    va_start(vargs, format);
    logger->log(logger, level, fileName, functionName, lineNumber, format,
                vargs);
    va_end(vargs);
    if (level == LOGLEVEL_ERROR) {
        logger_flush(logger);
    }
}

void logger_setLevel(Logger* logger, LogLevel level) {
    if (!logger) {
        return;
    }
    logger->setLevel(logger, level);
}

bool logger_isEnabled(Logger* logger, LogLevel level) {
    if (!logger) {
        return false;
    }

    return logger->isEnabled(logger, level);
}

void logger_flush(Logger* logger) {
    if (!logger) {
        return;
    }
    logger->flush(logger);
}
