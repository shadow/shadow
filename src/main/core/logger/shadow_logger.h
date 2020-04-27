/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_LOGGER_H_
#define SHD_LOGGER_H_

#include <glib.h>
#include <pthread.h>

#include "support/logger/log_level.h"

// ShadowLogger is a Logger that uses per-thread log queues to avoid global lock,
// and adds Shadow-specific context to each log entry.
typedef struct _ShadowLogger ShadowLogger;

ShadowLogger* shadow_logger_new(LogLevel filterLevel);

void shadow_logger_ref(ShadowLogger* logger);
void shadow_logger_unref(ShadowLogger* logger);

void shadow_logger_register(ShadowLogger* logger, pthread_t callerThread);
void shadow_logger_flushRecords(ShadowLogger* logger, pthread_t callerThread);
void shadow_logger_syncToDisk(ShadowLogger* logger);

void shadow_logger_setDefault(ShadowLogger* logger);
ShadowLogger* shadow_logger_getDefault();

void shadow_logger_setFilterLevel(ShadowLogger* logger, LogLevel level);
gboolean shadow_logger_shouldFilter(ShadowLogger* logger, LogLevel level);

void shadow_logger_setEnableBuffering(ShadowLogger* logger, gboolean enabled);

void shadow_logger_logVA(ShadowLogger* logger, LogLevel level, const gchar* fileName,
                      const gchar* functionName, const gint lineNumber,
                      const gchar* format, va_list vargs);
void shadow_logger_log(ShadowLogger* logger, LogLevel level, const gchar* fileName,
                    const gchar* functionName, const gint lineNumber,
                    const gchar* format, ...);

#endif /* SHD_LOGGER_H_ */
