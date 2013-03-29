/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_PING_SERVICE_H_
#define SHD_PING_SERVICE_H_

#include <glib.h>
#include <stddef.h>
#include <time.h>

#include "shd-ping-client.h"
#include "shd-ping-server.h"

enum pingService_loglevel {
	PING_CRITICAL, PING_WARNING, PING_NOTICE, PING_INFO, PING_DEBUG
};

#define PING_PORT 25
#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

typedef void (*pingService_log_cb)(enum pingService_loglevel level, const gchar* message);
typedef void (*pingService_sleep_cb)(gpointer sfg, guint seconds);
typedef in_addr_t (*pingService_hostbyname_cb)(const gchar* hostname);
typedef void (*pingService_createCallback_cb)(void *func, void *data, guint milliseconds);

typedef struct _PingService_Args PingService_Args;
struct _PingService_Args {
	pingService_log_cb log_cb;
	pingService_hostbyname_cb hostbyname_cb;
	pingService_createCallback_cb callback_cb;
	gchar *socksHostname;
	gchar *socksPort;
	gchar *pingInterval;
	gchar *pingSize;
};

typedef struct _PingService PingService;
struct _PingService {
	gint serverEpolld;
	gint clientEpolld;
	PingServer *server;
	PingClient *client;
	gint pingsTransfered;
	gint64 lastPingSent;
	gint64 lastPingRecv;

	pingService_hostbyname_cb hostbyname_cb;
	pingService_log_cb log_cb;
	pingService_createCallback_cb callback_cb;
	gchar logBuffer[1024];
};

int pingService_startNode(PingService *pingSvc, PingService_Args *args, gint serverEpolld, gint clientEpolld);
int pingService_activate(PingService *pingSvc, gint sockd, gint events, gint epolld);
int pingService_stop(PingService *pingSvc);

#endif
