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
