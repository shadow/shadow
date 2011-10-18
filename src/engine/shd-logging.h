/*
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

#ifndef SHD_LOGGING_H_
#define SHD_LOGGING_H_

#include <glib.h>

#define error(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)
#define critical(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__)
#define warning(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__)
#define message(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__)
#define info(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__)
#define debug(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)

void logging_handleLog(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);
void logging_logv(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, va_list vargs);
void logging_log(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, ...);

#endif /* SHD_LOGGING_H_ */
