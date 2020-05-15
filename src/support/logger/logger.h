#ifndef MAIN_CORE_LOGGER_LOGGER_H_
#define MAIN_CORE_LOGGER_LOGGER_H_

/*
 * A simple logger API.
 *
 * By default this simply writes to stderr. However, it also supports
 * overriding with a custom Logger.  Unlike in glib, when a custom Logger is
 * supplied, it's that logger's job to do any necessary synchronization. This
 * allows us to use a custom Logger in Shadow that avoids a global lock.
 */

#include <glib.h>

#include "support/logger/log_level.h"

/* convenience macros for logging messages at various levels */
// clang-format off
#define error(...)      logger_log(logger_getDefault(), LOGLEVEL_ERROR, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define critical(...)   logger_log(logger_getDefault(), LOGLEVEL_CRITICAL, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define warning(...)    logger_log(logger_getDefault(), LOGLEVEL_WARNING, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define message(...)    logger_log(logger_getDefault(), LOGLEVEL_MESSAGE, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define info(...)       logger_log(logger_getDefault(), LOGLEVEL_INFO, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#ifdef DEBUG
#define debug(...)      logger_log(logger_getDefault(), LOGLEVEL_DEBUG, __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#else
#define debug(...)
#endif
// clang-format on

typedef struct _Logger Logger;

// A custom logger is implemented by defining a struct with a `struct _Logger`
// as its first member.
struct _Logger {
    // Log the given information. This callback is responsible for any necessary
    // synchronization.
    void (*log)(Logger* logger, LogLevel level, const gchar* fileName,
                const gchar* functionName, const gint lineNumber,
                const gchar* format, va_list vargs);
    void (*destroy)(Logger* logger);
};

// Not thread safe. The previously set logger, if any, will be destroyed.
// `logger` may be NULL.
void logger_setDefault(Logger* logger);

// Not thread safe. May return NULL.
Logger* logger_getDefault();

// Thread safe. `logger` may be NULL, in which case glib's logging
// functionality will be used.
//
// The `__format__` attribute tells the compiler to apply the same format-string
// diagnostics that it does for `printf`.
// https://clang.llvm.org/docs/AttributeReference.html#format
// https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html#Common-Function-Attributes
__attribute__((__format__ (__printf__, 6, 7)))
void logger_log(Logger* logger, LogLevel level, const gchar* fileName,
                const gchar* functionName, const gint lineNumber,
                const gchar* format, ...);

#endif
