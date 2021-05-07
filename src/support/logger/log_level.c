/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "support/logger/log_level.h"

#include <glib.h>
#include <stddef.h>

const char* loglevel_toStr(LogLevel level) {
    switch (level) {
        case LOGLEVEL_ERROR: return "error";
        case LOGLEVEL_WARNING: return "warning";
        case LOGLEVEL_INFO: return "info";
        case LOGLEVEL_DEBUG: return "debug";
        case LOGLEVEL_TRACE: return "trace";
        case LOGLEVEL_UNSET:
        default: return "unset";
    }
}

LogLevel loglevel_fromStr(const char* levelStr) {
    if (levelStr == NULL) {
        return LOGLEVEL_UNSET;
    } else if (g_ascii_strcasecmp(levelStr, "error") == 0) {
        return LOGLEVEL_ERROR;
    } else if (g_ascii_strcasecmp(levelStr, "warning") == 0) {
        return LOGLEVEL_WARNING;
    } else if (g_ascii_strcasecmp(levelStr, "info") == 0) {
        return LOGLEVEL_INFO;
    } else if (g_ascii_strcasecmp(levelStr, "debug") == 0) {
        return LOGLEVEL_DEBUG;
    } else if (g_ascii_strcasecmp(levelStr, "trace") == 0) {
        return LOGLEVEL_TRACE;
    } else {
        return LOGLEVEL_UNSET;
    }
}
