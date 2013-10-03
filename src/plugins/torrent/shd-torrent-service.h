/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
