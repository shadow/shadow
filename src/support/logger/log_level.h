/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SUPPORT_LOGGER_LOG_LEVEL_H_
#define SUPPORT_LOGGER_LOG_LEVEL_H_

typedef enum _LogLevel LogLevel;
enum _LogLevel {
    LOGLEVEL_UNSET,
    LOGLEVEL_ERROR,
    LOGLEVEL_CRITICAL,
    LOGLEVEL_WARNING,
    LOGLEVEL_MESSAGE,
    LOGLEVEL_INFO,
    LOGLEVEL_DEBUG,
    LOGLEVEL_TRACE,
};

const char* loglevel_toStr(LogLevel level);
LogLevel loglevel_fromStr(const char* levelStr);

#endif /* SUPPORT_LOGGER_LOG_LEVEL_H_ */
