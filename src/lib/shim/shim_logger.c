#include "lib/shim/shim_logger.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_tls.h"

typedef struct _ShimLogger {
    Logger base;
    FILE* file;
    LogLevel level;
} ShimLogger;

static size_t _simulation_nanos_string(char* dst, size_t size) {
    const long nanos_per_sec = 1000000000l;

    uint64_t nanos = shim_sys_get_simtime_nanos();
    uint32_t seconds = nanos / nanos_per_sec;
    nanos = nanos % nanos_per_sec;
    uint32_t mins = seconds / 60;
    seconds = seconds % 60;
    uint32_t hours = mins / 60;
    mins = mins % 60;

    return snprintf(dst, size, "%02" PRIu32 ":%02" PRIu32 ":%02" PRIu32 ".%09" PRIu64, hours, mins,
                    seconds, nanos);
}

void shimlogger_log(Logger* base, LogLevel level, const char* fileName, const char* functionName,
                    const int lineNumber, const char* format, va_list vargs) {
    if (!logger_isEnabled(base, level)) {
        return;
    }

    static ShimTlsVar in_logger_var = {0};
    bool* in_logger = shimtlsvar_ptr(&in_logger_var, sizeof(*in_logger));

    // Stack-allocated to avoid dynamic allocation.
    char buf[2000];
    size_t offset = 0;
    if (*in_logger) {
        // Avoid recursion in logging around syscall handling.
        return;
    }
    *in_logger = true;
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    ShimLogger* logger = (ShimLogger*)base;

    // Keep appending to string. These functions all ensure NULL-byte termination.

    offset += logger_elapsed_string(&buf[offset], sizeof(buf) - offset);
    offset = MIN(offset, sizeof(buf));

    offset += snprintf(&buf[offset], sizeof(buf) - offset, " [");
    offset = MIN(offset, sizeof(buf));

    offset += _simulation_nanos_string(&buf[offset], sizeof(buf) - offset);
    offset = MIN(offset, sizeof(buf));

    offset += snprintf(&buf[offset], sizeof(buf) - offset, "] [shd-shim] [%s] [%s:%i] [%s] ",
                       loglevel_toStr(level), logger_base_name(fileName), lineNumber, functionName);
    offset = MIN(offset, sizeof(buf));

    offset += vsnprintf(&buf[offset], sizeof(buf) - offset, format, vargs);
    offset = MIN(offset, sizeof(buf) - 1); // Leave room for newline.
    buf[offset++] = '\n';

    // We can't use `fwrite` here, since it internally uses locking
    // making it definitely not async-signal-safe. (See signal-safety(7)).
    //
    // `fwrite_unlocked` seems like it probably *ought* to be ok to use,
    // but in practice seems to result in difficult-to-debug failures
    // when running tor in preload+seccomp mode.
    //
    // `write` *is* guaranteed to be async-signal-safe.
    if (write(fileno(logger->file), buf, offset) < 0) {
        abort();
    }

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
    *in_logger = false;
}

void shimlogger_destroy(Logger* logger) {
    free(logger);
}

void shimlogger_flush(Logger* base) {
    ShimLogger* logger = (ShimLogger*)base;
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);
    fflush_unlocked(logger->file);
    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

bool shimlogger_isEnabled(Logger* base, LogLevel level) {
    ShimLogger* logger = (ShimLogger*)base;
    return level <= logger->level;
}

void shimlogger_setLevel(Logger* base, LogLevel level) {
    ShimLogger* logger = (ShimLogger*)base;
    logger->level = level;
}

Logger* shimlogger_new(FILE* file) {
    ShimLogger* logger = malloc(sizeof(*logger));
    #ifdef DEBUG
        LogLevel level = LOGLEVEL_TRACE;
    #else
        LogLevel level = LOGLEVEL_INFO;
    #endif
    *logger = (ShimLogger){
        .base =
            {
                .log = shimlogger_log,
                .destroy = shimlogger_destroy,
                .flush = shimlogger_flush,
                .isEnabled = shimlogger_isEnabled,
                .setLevel = shimlogger_setLevel,
            },
        .file = file,
        .level = level,
    };
    return (Logger*)logger;
}
