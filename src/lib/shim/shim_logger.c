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
#include "lib/shim/shim_syscall.h"

typedef struct _ShimLogger {
    Logger base;
    FILE* file;
    LogLevel level;
} ShimLogger;

static size_t _simulation_nanos_string(char* dst, size_t size) {
    uint64_t simulation_nanos = shim_syscall_get_simtime_nanos();
    const long nanos_per_sec = 1000000000l;
    time_t seconds = simulation_nanos / nanos_per_sec;
    uint64_t nanos = simulation_nanos % nanos_per_sec;
    struct tm tm;
    gmtime_r(&seconds, &tm);
    return snprintf(
        dst, size, "%02d:%02d:%02d.%09" PRIu64, tm.tm_hour, tm.tm_min, tm.tm_sec, nanos);
}

void shimlogger_log(Logger* base, LogLevel level, const char* fileName, const char* functionName,
                    const int lineNumber, const char* format, va_list vargs) {
    if (!logger_isEnabled(base, level)) {
        return;
    }
    static __thread bool in_logger = false;
    // Stack-allocated to avoid dynamic allocation.
    char buf[200];
    size_t offset = 0;
    if (in_logger) {
        // Avoid recursion in logging around syscall handling.
        return;
    }
    in_logger = true;
    shim_disableInterposition();

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

    // We avoid locked IO here, since it can result in deadlock if Shadow
    // forcibly stops this thread while that lock is still held. Interleaved
    // writes shouldn't be a problem since Shadow only allows one plugin thread
    // at a time to execute, and doesn't switch threads on file syscalls.
    //
    // This could be reverted to a normal `fwrite` if we end up having Shadow
    // emulate syscalls inside the shim, since then Shadow would correctly
    // block a thread on the fwrite-lock (if it's locked) and switch to one
    // that's runnable.
    fwrite_unlocked(buf, 1, offset, logger->file);

    if (logger->level == LOGLEVEL_TRACE || level == LOGLEVEL_ERROR) {
        fflush_unlocked(logger->file);
    }
    shim_enableInterposition();
    in_logger = false;
}

void shimlogger_destroy(Logger* logger) {
    free(logger);
}

void shimlogger_flush(Logger* base) {
    ShimLogger* logger = (ShimLogger*)base;
    shim_disableInterposition();
    fflush_unlocked(logger->file);
    shim_enableInterposition();
}

bool shimlogger_isEnabled(Logger* base, LogLevel level) {
    ShimLogger* logger = (ShimLogger*)base;
    return level >= logger->level;
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
