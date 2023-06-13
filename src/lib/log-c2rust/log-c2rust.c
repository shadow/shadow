#include "lib/log-c2rust/log-c2rust.h"

#include "lib/log-c2rust/rustlogger.h"
#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"

static void _log(Logger* logger, LogLevel level, const char* fileName, const char* functionName,
                 const int lineNumber, const char* format, va_list vargs) {
    rustlogger_log(level, fileName, functionName, lineNumber, format, vargs);
}

static void _flush(Logger* logger) { rustlogger_flush(); }

static bool _isEnabled(Logger* logger, LogLevel level) { return rustlogger_isEnabled(level); }

static void _setLevel(Logger* logger, LogLevel level) {
    warning("Setting the log level is not supported");
}

Logger* rustlogger_new() {
    Logger* logger = malloc(sizeof(*logger));
    *logger = (Logger){
        .log = _log,
        .flush = _flush,
        .destroy = rustlogger_destroy,
        .isEnabled = _isEnabled,
        .setLevel = _setLevel,
    };
    return logger;
}

void rustlogger_destroy(Logger* logger) { free(logger); }
