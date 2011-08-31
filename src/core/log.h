/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#ifndef _log_h
#define _log_h

#include <glib.h>
#include <stdio.h>

#include "log_codes.h"
#include "socket.h"
#include "nbdf.h"
#include "pipecloud.h"

void dlogf(enum shadow_log_code level, gchar *fmt, ...);
void dlogf_main(enum shadow_log_code level, enum shadow_log_context context, gchar *fmt, va_list vargs);
void dlogf_bin(gchar * d, gint length);
gchar* dlog_get_status_prefix(gchar* caller_str);

#ifdef DEBUG
#define debugf(fmt, ...) dlogf(LOG_DEBUG, fmt, ## __VA_ARGS__)
#else
#define debugf(fmt, ...)
#endif

#define inet_ntoa_t(ip) inet_ntoa((struct in_addr){ip})

void dlog_init(gchar* loglevel);
void dlog_cleanup(void);
void dlog_setprefix(gchar *);
void dlog_deposit(gint frametype, nbdf_tp frame);
void dlog_close_channel(gint channel);
void dlog_set_channel(gint channel, gchar * destination, gint process_identifier) ;
void dlog_update_status(void);
void dlog_channel_write(gint channel, gchar * data, guint length);
void dlog_set_pipecloud(pipecloud_tp pipecloud);
void dlog_set_dvn_routing(gint enabled);

#define LOGGER_TYPE_NULL 0
#define LOGGER_TYPE_FILE 1
#define LOGGER_TYPE_MYSQL 2
#define LOGGER_TYPE_SOCKET 3
#define LOGGER_TYPE_STDOUT 4

#define LOG_NUM_CHANNELS 10

typedef struct logger_t {
	gint type;
	gint level;

	union {
		struct {
			FILE * file;
			gchar path[256];
		} file ;

		struct {
			gchar host[128];
			gint port;

			gchar dbname[128];
			gchar username[128];
			gchar password[128];
		} mysql ;

		struct {
			gchar host[128];
			gint port;
			socket_tp sock;
		} tcpsocket;
	} detail;
} logger_t, * logger_tp;

#endif
