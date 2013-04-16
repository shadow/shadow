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

#ifndef SHD_SERVICE_TORRENT_H_
#define SHD_SERVICE_TORRENT_H_

#include <glib.h>
#include <stddef.h>
#include <time.h>

#include "shd-torrent-authority.h"
#include "shd-torrent-client.h"
#include "shd-torrent-server.h"

enum torrentService_loglevel {
	TSVC_CRITICAL, TSVC_WARNING, TSVC_NOTICE, TSVC_INFO, TSVC_DEBUG
};

typedef void (*torrentService_log_cb)(enum torrentService_loglevel level, const gchar* message);
typedef void (*torrentService_sleep_cb)(gpointer sfg, guint seconds);
typedef in_addr_t (*torrentService_hostbyname_cb)(const gchar* hostname);

typedef struct _TorrentService_AuthorityArgs TorrentService_AuthorityArgs;
struct _TorrentService_AuthorityArgs {
	torrentService_log_cb log_cb;
	torrentService_hostbyname_cb hostbyname_cb;
	gchar *port;
};

typedef struct _TorrentService_NodeArgs TorrentService_NodeArgs;
struct _TorrentService_NodeArgs {
	torrentService_log_cb log_cb;
	torrentService_hostbyname_cb hostbyname_cb;
	torrentService_sleep_cb sleep_cb;
	gchar *nodeType;
	gchar *authorityHostname;
	gchar *authorityPort;
	gchar *socksHostname;
	gchar *socksPort;
	gchar *serverPort;
	gchar *fileSize;
	gchar *downBlockSize;
	gchar *upBlockSize;
};

typedef struct _TorrentService TorrentService;
struct _TorrentService {
	TorrentServer* server;
	TorrentClient* client;
	TorrentAuthority* authority;

	torrentService_hostbyname_cb hostbyname_cb;
	torrentService_log_cb log_cb;
	torrentService_sleep_cb sleep_cb;
	gchar logBuffer[1024];

	struct timespec lastReport;
	gint clientDone;
	struct timespec wakeupTime;
};

int torrentService_startAuthority(TorrentService *tsvc, TorrentService_AuthorityArgs *args, gint epolld, gint* sockd_out);
int torrentService_startNode(TorrentService *tsvc, TorrentService_NodeArgs *args, gint serverEpolld, gint clientEpolld, gint* sockd_out);
int torrentService_activate(TorrentService *tsvc, gint sockd, gint events, gint epolld);
int torrentService_stop(TorrentService *tsvc);

#endif
