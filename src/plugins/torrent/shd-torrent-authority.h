/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TORRENT_AUTHORITY_H_
#define SHD_TORRENT_AUTHORITY_H_

#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>


enum torrentAuthority_code {
	TA_SUCCESS, TA_CLOSED, TA_ERR_INVALID, TA_ERR_FATAL, TA_ERR_BADSD, TA_ERR_WOULDBLOCK, TA_ERR_BUFSPACE,
	TA_ERR_SOCKET, TA_ERR_BIND, TA_ERR_LISTEN, TA_ERR_ACCEPT, TA_ERR_RECV, TA_ERR_SEND, TA_ERR_CLOSE, TA_ERR_EPOLL,
	TA_ERR_NOCONN,
};

enum torrentAuthority_messages {
	TA_MSG_REGISTER = 1,
	TA_MSG_REQUEST_NODES = 2,
};

enum torrentAuthority_loglevel {
	TA_CRITICAL, TA_WARNING, TA_NOTICE, TA_INFO, TA_DEBUG
};

typedef void (*torrentAuthority_log_cb)(enum torrentAuthority_loglevel level, const gchar* message);

typedef struct _TorrentAuthority_Args TorrentAuthority_Args;
struct _TorrentAuthority_Args {
	gchar *authPort;
};

typedef struct _TorrentAuthority_Connection TorrentAuthority_Connection;
struct _TorrentAuthority_Connection {
	gint sockd;
	in_addr_t addr;
	in_port_t serverPort;
};

/**
 *
 */
typedef struct _TorrentAuthority TorrentAuthority;
struct _TorrentAuthority {
	gint epolld;
	gint listenSockd;
	GList *servers, *clients;
	GHashTable *connections;
	//GHashTable *nodes;
	torrentAuthority_log_cb log_cb;
	gchar logBuffer[1024];
};

gint torrentAuthority_start(TorrentAuthority* ta, gint epolld, in_addr_t listenIP, in_port_t listenPort, gint maxConnections);
gint torrentAuthority_activate(TorrentAuthority* ta, gint sockd);
gint torrentAuthority_accept(TorrentAuthority* ta, gint* sockdOut);
gint torrentAuthority_shutdown(TorrentAuthority* ta);

#endif /* SHD_TORRENT_AUTHORITY_H_ */
