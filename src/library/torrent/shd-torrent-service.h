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
	gchar logBuffer[1024];

	struct timespec lastReport;
	gint clientDone;
};

int torrentService_startAuthority(TorrentService *tsvc, TorrentService_AuthorityArgs *args, gint epolld, gint* sockd_out);
int torrentService_startNode(TorrentService *tsvc, TorrentService_NodeArgs *args, gint serverEpolld, gint clientEpolld, gint* sockd_out);
int torrentService_activate(TorrentService *tsvc, gint sockd, gint events, gint epolld);
int torrentService_stop(TorrentService *tsvc);


#endif
