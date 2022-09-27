/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "lib/logger/log_level.h"

#include <stddef.h>
#include <string.h>

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
    } else if (strcmp(levelStr, "error") == 0) {
        return LOGLEVEL_ERROR;
    } else if (strcmp(levelStr, "warning") == 0) {
        return LOGLEVEL_WARNING;
    } else if (strcmp(levelStr, "info") == 0) {
        return LOGLEVEL_INFO;
    } else if (strcmp(levelStr, "debug") == 0) {
        return LOGLEVEL_DEBUG;
    } else if (strcmp(levelStr, "trace") == 0) {
        return LOGLEVEL_TRACE;
    } else {
        return LOGLEVEL_UNSET;
    }
}
