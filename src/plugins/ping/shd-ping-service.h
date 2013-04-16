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
