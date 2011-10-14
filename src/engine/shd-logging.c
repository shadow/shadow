/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shadow.h"

static gchar* _logging_getLogLevelString(GLogLevelFlags log_level) {
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
	return levels;
}

static const gchar* _logging_getLogDomainString(const gchar *log_domain) {
	const gchar* domains = log_domain != NULL ? log_domain : "shadow";
	return domains;
}

void logging_handleLog(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data) {
	/* callback from GLib, no access to workers */
	GDateTime* dt_now = g_date_time_new_now_local();
	gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S:%N");

	g_print("%s %s\n", dt_format, message);

	if(log_level & G_LOG_LEVEL_ERROR) {
		g_print("\t**aborting**\n");
	}

	g_date_time_unref(dt_now);
	g_free(dt_format);
}

void logging_logv(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, va_list vargs) {
	/* this is called by worker threads, so we have access to worker */
	Worker* w = worker_getPrivate();

	GString* simtime = g_string_new(NULL);
	if(w->clock_now == SIMTIME_INVALID) {
		g_string_printf(simtime, "n/a");
	} else {
		SimulationTime hours, minutes, seconds, remainder;
		remainder = w->clock_now;

		hours = remainder / SIMTIME_ONE_HOUR;
		remainder %= SIMTIME_ONE_HOUR;
		minutes = remainder / SIMTIME_ONE_MINUTE;
		remainder %= SIMTIME_ONE_MINUTE;
		seconds = remainder / SIMTIME_ONE_SECOND;
		remainder %= SIMTIME_ONE_SECOND;

		g_string_printf(simtime, "%lu:%lu:%lu:%lu", hours, minutes, seconds, remainder);
	}

	gchar* clock_string = g_string_free(simtime, FALSE);

	const gchar* functionString = functionName ? functionName : "n/a";

	GString* string_buffer = g_string_new(NULL);
	g_string_printf(string_buffer, "%s [t%i] [%s-%s] [%s] %s",
			clock_string,
			w->thread_id,
			_logging_getLogDomainString(log_domain),
			_logging_getLogLevelString(log_level),
			functionString,
			format
			);

	gchar* new_format = g_string_free(string_buffer, FALSE);
	g_logv(log_domain, log_level, new_format, vargs);
	g_free(new_format);
	g_free(clock_string);
}

void logging_log(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, ...) {
	va_list vargs;
	va_start(vargs, format);

	logging_logv(log_domain, log_level, functionName, format, vargs);

	va_end(vargs);
}
