#include "shim/shim_logger.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <time.h>

#include "shim/shim.h"
#include "support/logger/log_level.h"
#include "support/logger/logger.h"

typedef struct _ShimLogger {
    Logger base;
    FILE* file;
} ShimLogger;

static uint64_t _simulation_nanos = 0;
void shimlogger_set_simulation_nanos(uint64_t simulation_nanos) {
    _simulation_nanos = simulation_nanos;
}
uint64_t shimlogger_get_simulation_nanos() {
    return _simulation_nanos;
}

static size_t _simulation_nanos_string(char* dst, size_t size) {
    const long nanos_per_sec = 1000000000l;
    time_t seconds = _simulation_nanos / nanos_per_sec;
    uint64_t nanos = _simulation_nanos % nanos_per_sec;
    struct tm tm;
    gmtime_r(&seconds, &tm);
    return snprintf(
        dst, size, "%02d:%02d:%02d.%09" PRIu64, tm.tm_hour, tm.tm_min, tm.tm_sec, nanos);
}

void shimlogger_log(Logger* base, LogLevel level, const char* fileName, const char* functionName,
                    const int lineNumber, const char* format, va_list vargs) {
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

#ifdef DEBUG
    fflush_unlocked(logger->file);
#endif
    shim_enableInterposition();
    in_logger = false;
}

void shimlogger_destroy(Logger* logger) {
    free(logger);
}

Logger* shimlogger_new(FILE* file) {
    ShimLogger* logger = malloc(sizeof(*logger));
    *logger = (ShimLogger){
        .base =
            {
                .log = shimlogger_log,
                .destroy = shimlogger_destroy,
            },
        .file = file,
    };
    return (Logger*)logger;
}
