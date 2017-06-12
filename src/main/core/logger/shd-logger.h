/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_LOGGER_H_
#define SHD_LOGGER_H_

/* convenience macros for logging messages at various levels */
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

typedef struct _Logger Logger;

Logger* logger_new(LogLevel filterLevel);

void logger_ref(Logger* logger);
void logger_unref(Logger* logger);

void logger_register(Logger* logger, pthread_t callerThread);
void logger_flushRecords(Logger* logger, pthread_t callerThread);
void logger_syncToDisk(Logger* logger);

void logger_setDefault(Logger* logger);
Logger* logger_getDefault();

void logger_setFilterLevel(Logger* logger, LogLevel level);
gboolean logger_shouldFilter(Logger* logger, LogLevel level);

void logger_setEnableBuffering(Logger* logger, gboolean enabled);

void logger_logVA(Logger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, va_list vargs);
void logger_log(Logger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, ...);

#endif /* SHD_LOGGER_H_ */
