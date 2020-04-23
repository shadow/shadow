#include "support/logger/logger.h"

static Logger* defaultLogger = NULL;

void logger_setDefault(Logger* logger) {
    if (defaultLogger != NULL) {
        defaultLogger->destroy(defaultLogger);
    }
    defaultLogger = logger;
}

Logger* logger_getDefault() { return defaultLogger; }

static GLogLevelFlags _gloglevelflags(LogLevel level) {
    switch (level) {
        case LOGLEVEL_CRITICAL: return G_LOG_LEVEL_CRITICAL;
        case LOGLEVEL_WARNING: return G_LOG_LEVEL_WARNING;
        case LOGLEVEL_MESSAGE: return G_LOG_LEVEL_MESSAGE;
        case LOGLEVEL_INFO: return G_LOG_LEVEL_INFO;
        case LOGLEVEL_DEBUG: return G_LOG_LEVEL_DEBUG;
        case LOGLEVEL_UNSET:
        case LOGLEVEL_ERROR:
        default: return G_LOG_LEVEL_ERROR;
    }
}

static void _logger_default_log(LogLevel level, const gchar* fileName,
                                const gchar* functionName,
                                const gint lineNumber, const gchar* format,
                                va_list vargs) {
    gchar* message = g_strdup_vprintf(format, vargs);
    g_log("shadow", _gloglevelflags(level), "[%s:%i] [%s] %s", fileName,
          lineNumber, functionName, message);
    g_free(message);
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
