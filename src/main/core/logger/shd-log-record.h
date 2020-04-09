/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_LOG_RECORD_H_
#define SHD_LOG_RECORD_H_

#include <glib.h>

#include "core/logger/shd-log-level.h"
#include "core/support/shd-definitions.h"

typedef struct _LogRecord LogRecord;

LogRecord* logrecord_new(LogLevel level, gdouble timespan, const gchar* fileName, const gchar* functionName, const gint lineNumber);

void logrecord_ref(LogRecord* record);
void logrecord_unref(LogRecord* record);

gint logrecord_compare(const LogRecord* a, const LogRecord* b, gpointer userData);
void logrecord_setTime(LogRecord* record, SimulationTime simElapsedNanos);
void logrecord_setNames(LogRecord* record, const gchar* threadName, const gchar* hostName);
void logrecord_formatMessageVA(LogRecord* record, const gchar *messageFormat, va_list vargs);
void logrecord_formatMessage(LogRecord* record, const gchar *messageFormat, ...);

gchar* logrecord_toString(LogRecord* record);

#endif /* SHD_LOG_RECORD_H_ */
