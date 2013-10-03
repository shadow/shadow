/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

static gchar* _logging_getNewLogLevelString(GLogLevelFlags log_level) {
	gchar* levels;
	switch (log_level) {
		case G_LOG_LEVEL_ERROR: {
			levels = "error";
			break;
		}
		case G_LOG_LEVEL_CRITICAL: {
			levels = "critical";
			break;
		}

		case G_LOG_LEVEL_WARNING: {
			levels = "warning";
			break;
		}

		case G_LOG_LEVEL_MESSAGE: {
			levels = "message";
			break;
		}

		case G_LOG_LEVEL_INFO: {
			levels = "info";
			break;
		}

		case G_LOG_LEVEL_DEBUG: {
			levels = "debug";
			break;
		}

		default: {
			levels = "default";
			break;
		}
	}
	return g_strdup(levels);
}

static gboolean _logging_messageIsFiltered(const gchar *msgLogDomain, GLogLevelFlags msgLogLevel) {
	Worker* w = worker_getPrivate();

	/* check the local node log level first */
	gboolean isNodeLevelSet = FALSE;
	if(w->cached_node) {
		GLogLevelFlags nodeLevel = host_getLogLevel(w->cached_node);
		if(nodeLevel) {
			isNodeLevelSet = TRUE;
			if(msgLogLevel > nodeLevel) {
				return TRUE;
			}
		}
	}

	/* only check the global config if the node didnt have a local setting */
	if(!isNodeLevelSet && w->cached_engine) {
		Configuration* c = engine_getConfig(w->cached_engine);
		if(c && (msgLogLevel > configuration_getLogLevel(c))) {
			return TRUE;
		}
	}

	return FALSE;
}

/* this func is called whenever g_logv is called, not just in our log code */
void logging_handleLog(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
	/* GLogLevelFlags* configuredLogLevel = user_data; */
	const gchar* logDomainStr = log_domain ? log_domain : "shadow";
	const gchar* messageStr = message ? message : "n/a";

	/* check again if the message should be filtered */
	if(_logging_messageIsFiltered(logDomainStr, log_level)) {
		return;
	}

	gulong hours = 0, minutes = 0, seconds = 0, microseconds = 0;
	gulong elapsed = (gulong) g_timer_elapsed(engine_getRunTimer(shadow_engine), &microseconds);
	hours = elapsed / 3600;
	elapsed %= 3600;
	minutes = elapsed / 60;
	seconds = elapsed % 60;

	g_print("%lu:%lu:%lu:%06lu %s\n", hours, minutes, seconds, microseconds, messageStr);

	if(log_level & G_LOG_LEVEL_ERROR) {
		g_print("%s", "\t**aborting**\n");
	}
}

void logging_logv(const gchar *msgLogDomain, GLogLevelFlags msgLogLevel,
		const gchar* functionName, const gchar *format, va_list vargs) {
	/* this is called by worker threads, so we have access to worker */
	Worker* w = worker_getPrivate();

	/* see if we can avoid some work because the message is filtered anyway */
	const gchar* logDomainStr = msgLogDomain ? msgLogDomain : "shadow";
	if(_logging_messageIsFiltered(logDomainStr, msgLogLevel)) {
		return;
	}

	const gchar* logFunctionStr = functionName ? functionName : "n/a";
	const gchar* formatStr = format ? format : "n/a";
	gchar* logLevelStr = _logging_getNewLogLevelString(msgLogLevel);

	/* format the simulation time if we are running an event */
	GString* clockStringBuffer = g_string_new("");
	if(w->clock_now != SIMTIME_INVALID) {
		SimulationTime hours, minutes, seconds, remainder;
		remainder = w->clock_now;

		hours = remainder / SIMTIME_ONE_HOUR;
		remainder %= SIMTIME_ONE_HOUR;
		minutes = remainder / SIMTIME_ONE_MINUTE;
		remainder %= SIMTIME_ONE_MINUTE;
		seconds = remainder / SIMTIME_ONE_SECOND;
		remainder %= SIMTIME_ONE_SECOND;

		g_string_printf(clockStringBuffer, "%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT":%09"G_GUINT64_FORMAT"", hours, minutes, seconds, remainder);
	} else {
		g_string_printf(clockStringBuffer, "n/a");
	}

	/* we'll need to free clockString later */
	gchar* clockString = g_string_free(clockStringBuffer, FALSE);

	/* node identifier, if we are running a node
	 * dont free this since we dont own the ip address string */
	GString* nodeStringBuffer = g_string_new("");
	if(w->cached_node) {
		g_string_printf(nodeStringBuffer, "%s-%s", host_getName(w->cached_node), host_getDefaultIPName(w->cached_node));
	} else {
		g_string_printf(nodeStringBuffer, "n/a");
	}
	gchar* nodeString = g_string_free(nodeStringBuffer, FALSE);

	/* the function name - no need to free this */
	GString* newLogFormatBuffer = g_string_new(NULL);
	g_string_printf(newLogFormatBuffer, "[thread-%i] %s [%s-%s] [%s] [%s] %s",
			w->thread_id, clockString, logDomainStr, logLevelStr, nodeString, logFunctionStr, formatStr);

	/* get the new format out of our string buffer and log it */
	gchar* newLogFormat = g_string_free(newLogFormatBuffer, FALSE);
	g_logv(logDomainStr, msgLogLevel, newLogFormat, vargs);

	/* cleanup */
	g_free(newLogFormat);
	g_free(clockString);
	g_free(nodeString);
	g_free(logLevelStr);
}

void logging_log(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, ...) {
	va_list vargs;
	va_start(vargs, format);

	logging_logv(log_domain, log_level, functionName, format, vargs);

	va_end(vargs);
}
