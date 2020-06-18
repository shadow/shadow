#include "shim/shim_logger.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "shim/shim.h"
#include "support/logger/log_level.h"
#include "support/logger/logger.h"

typedef struct _ShimLogger {
    Logger base;
    FILE* file;
} ShimLogger;

static uint64_t _simulation_nanos;
void shimlogger_set_simulation_nanos(uint64_t simulation_nanos) {
    _simulation_nanos = simulation_nanos;
}

static gchar* _simulation_nanos_string() {
    const long nanos_per_sec = 1000000000l;
    time_t seconds = _simulation_nanos / nanos_per_sec;
    uint64_t nanos = _simulation_nanos % nanos_per_sec;
    struct tm tm;
    gmtime_r(&seconds, &tm);
    return g_strdup_printf(
        "%02d:%02d:%02d.%09" PRIu64, tm.tm_hour, tm.tm_min, tm.tm_sec, nanos);
}

void shimlogger_log(Logger* base, LogLevel level, const gchar* fileName,
                    const gchar* functionName, const gint lineNumber,
                    const gchar* format, va_list vargs) {
    static __thread bool in_logger = false;
    if (in_logger) {
        // Avoid recursion in logging around syscall handling.
        return;
    }
    in_logger = true;
    shim_disableInterposition();

    ShimLogger* logger = (ShimLogger*)base;

    gchar* message = g_strdup_vprintf(format, vargs);
    gchar* time_string = logger_elapsed_string();
    gchar* simulation_nanos_string = _simulation_nanos_string();
    fprintf(logger->file, "%s [%s] [shd-shim] [%s] [%s:%i] [%s] %s\n",
            time_string, simulation_nanos_string, loglevel_toStr(level), fileName,
            lineNumber, functionName, message);
#ifdef DEBUG
    fflush(logger->file);
#endif
    g_free(message);
    g_free(time_string);
    g_free(simulation_nanos_string);
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
