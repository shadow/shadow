/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_LOGGER_H_
#define SHD_LOGGER_H_

#include <glib.h>
#include <pthread.h>

#include "support/logger/log_level.h"

// ShdLogger is a Logger that uses per-thread log queues to avoid global lock,
// and adds Shadow-specific context to each log entry.
typedef struct _ShdLogger ShdLogger;

ShdLogger* shd_logger_new(LogLevel filterLevel);

void shd_logger_ref(ShdLogger* logger);
void shd_logger_unref(ShdLogger* logger);

void shd_logger_register(ShdLogger* logger, pthread_t callerThread);
void shd_logger_flushRecords(ShdLogger* logger, pthread_t callerThread);
void shd_logger_syncToDisk(ShdLogger* logger);

void shd_logger_setDefault(ShdLogger* logger);
ShdLogger* shd_logger_getDefault();

void shd_logger_setFilterLevel(ShdLogger* logger, LogLevel level);
gboolean shd_logger_shouldFilter(ShdLogger* logger, LogLevel level);

void shd_logger_setEnableBuffering(ShdLogger* logger, gboolean enabled);

void shd_logger_logVA(ShdLogger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, va_list vargs);
void shd_logger_log(ShdLogger* logger, LogLevel level, const gchar* fileName, const gchar* functionName,
        const gint lineNumber, const gchar *format, ...);

#endif /* SHD_LOGGER_H_ */
