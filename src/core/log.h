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

#include <stdio.h>

#include "log_codes.h"
#include "socket.h"
#include "nbdf.h"
#include "pipecloud.h"

void dlogf(enum shadow_log_code level, char *fmt, ...);
void dlogf_main(enum shadow_log_code level, enum shadow_log_context context, char *fmt, va_list vargs);
void dlogf_bin(char * d, int length);
char* dlog_get_status_prefix(char* caller_str);

#ifdef DEBUG
#define debugf(fmt, ...) dlogf(LOG_DEBUG, fmt, ## __VA_ARGS__)
#else
#define debugf(fmt, ...)
#endif

#define inet_ntoa_t(ip) inet_ntoa((struct in_addr){ip})

void dlog_init(char* loglevel);
void dlog_cleanup(void);
void dlog_setprefix(char *);
void dlog_deposit(int frametype, nbdf_tp frame);
void dlog_close_channel(int channel);
void dlog_set_channel(int channel, char * destination, int process_identifier) ;
void dlog_update_status(void);
void dlog_channel_write(int channel, char * data, unsigned int length);
void dlog_set_pipecloud(pipecloud_tp pipecloud);
void dlog_set_dvn_routing(int enabled);

#define LOGGER_TYPE_NULL 0
#define LOGGER_TYPE_FILE 1
#define LOGGER_TYPE_MYSQL 2
#define LOGGER_TYPE_SOCKET 3
#define LOGGER_TYPE_STDOUT 4

#define LOG_NUM_CHANNELS 10

typedef struct logger_t {
	int type;
	int level;

	union {
		struct {
			FILE * file;
			char path[256];
		} file ;

		struct {
			char host[128];
			int port;

			char dbname[128];
			char username[128];
			char password[128];
		} mysql ;

		struct {
			char host[128];
			int port;
			socket_tp sock;
		} tcpsocket;
	} detail;
} logger_t, * logger_tp;

#endif
