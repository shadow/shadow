/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#ifndef SHD_LOGGING_H_
#define SHD_LOGGING_H_

#include <glib.h>

/**
 * @addtogroup Logging
 * @{
 * Use this module to log messages.
 */

/**
 * A convenience macro for logging a message at the error level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define error(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)

/**
 * A convenience macro for logging a message at the critical level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define critical(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__)

/**
 * A convenience macro for logging a message at the warning level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define warning(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__)

/**
 * A convenience macro for logging a message at the message level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define message(...) 	logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__)

/**
 * A convenience macro for logging a message at the info level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define info(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__)

/**
 * A convenience macro for logging a message at the debug level in the
 * default domain. Forwards to logging_log().
 *
 * @see logging_log()
 */
#define debug(...) 		logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)

/**
 * A log handler compatible with the GLib logging subsystem.
 *
 * This method can be registered as a log handler, e.g. with
 * g_log_set_default_handler(), and is responsible for logging the actual
 * message to various outputs. Real time is prepended to the message.
 *
 * If the log level is G_LOG_LEVEL_ERROR, an abort string is printed before
 * GLib aborts the program.
 *
 * @param log_domain a string representing a log domain, normally G_LOG_DOMAIN
 * @param log_level the level at which to log the message, one of GLogLevelFlags
 * @param message the message string
 * @param user_data a pointer to a gint representing the configured log level
 */
void logging_handleLog(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data);

/**
 * Low level logging function for logging messages from within a node context.
 *
 * Simulation information is prepended to the message to create a standard
 * log entry identifying the node and log level. The modified message is logged
 * internally with g_logv, which will call the registered log handlers for
 * logging the actual message.
 *
 * In most cases, it is more useful to call logging_log().
 *
 * @param msgLogDomain a string representing a log domain, normally G_LOG_DOMAIN
 * @param msgLogLevel the level at which to log the message, one of GLogLevelFlags
 * @param functionName the name of the calling function, usually __FUNCTION__
 * can be used in the calling progress
 * @param format a printf() style format string for logging
 * @param vargs a variable argument list corresponding to the format string
 *
 * @see logging_log()
 */
void logging_logv(const gchar *msgLogDomain, GLogLevelFlags msgLogLevel, const gchar* functionName, const gchar *format, va_list vargs);

/**
 * High level logging function for logging messages from within a node context.
 *
 * Extracts the variable argument list from a printf() style format string and
 * logs the message with logging_logv().
 *
 * @param log_domain a string representing a log domain, normally G_LOG_DOMAIN
 * @param log_level the level at which to log the message, one of GLogLevelFlags
 * @param functionName the name of the calling function, usually __FUNCTION__
 * can be used in the calling progress
 * @param format a printf() style format string for logging
 *
 * @see logging_logv()
 */
void logging_log(const gchar *log_domain, GLogLevelFlags log_level, const gchar* functionName, const gchar *format, ...);

/** @} */

#endif /* SHD_LOGGING_H_ */
