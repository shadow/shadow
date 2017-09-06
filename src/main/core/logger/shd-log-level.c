/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

const char* loglevel_toStr(LogLevel level) {
    switch (level) {
        case LOGLEVEL_ERROR:
            return "error";
        case LOGLEVEL_CRITICAL:
            return "critical";
        case LOGLEVEL_WARNING:
            return "warning";
        case LOGLEVEL_MESSAGE:
            return "message";
        case LOGLEVEL_INFO:
            return "info";
        case LOGLEVEL_DEBUG:
            return "debug";
        case LOGLEVEL_UNSET:
        default:
            return "unset";
    }
}

LogLevel loglevel_fromStr(const char* levelStr) {
    if(levelStr == NULL) {
        return LOGLEVEL_UNSET;
    } else if (g_ascii_strcasecmp(levelStr, "error") == 0) {
        return LOGLEVEL_ERROR;
    } else if (g_ascii_strcasecmp(levelStr, "critical") == 0) {
        return LOGLEVEL_CRITICAL;
    } else if (g_ascii_strcasecmp(levelStr, "warning") == 0) {
        return LOGLEVEL_WARNING;
    } else if (g_ascii_strcasecmp(levelStr, "message") == 0) {
        return LOGLEVEL_MESSAGE;
    } else if (g_ascii_strcasecmp(levelStr, "info") == 0) {
        return LOGLEVEL_INFO;
    } else if (g_ascii_strcasecmp(levelStr, "debug") == 0) {
        return LOGLEVEL_DEBUG;
    } else {
        return LOGLEVEL_UNSET;
    }
}
