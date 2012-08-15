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

#ifndef SHD_TORRENT_SERVER_H_
#define SHD_TORRENT_SERVER_H_

#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#define TS_BUF_SIZE 16384

enum torrentServer_code {
	TS_SUCCESS, TS_CLOSED, TS_ERR_INVALID, TS_ERR_FATAL, TS_ERR_BADSD, TS_ERR_WOULDBLOCK, TS_ERR_BUFSPACE,
	TS_ERR_SOCKET, TS_ERR_BIND, TS_ERR_LISTEN, TS_ERR_ACCEPT, TS_ERR_RECV, TS_ERR_SEND, TS_ERR_CLOSE, TS_ERR_EPOLL, TS_ERR_NOCONN
};

enum torrentServer_state {
	TS_IDLE,
	TS_REQUEST,
	TS_TRANSFER,
	TS_FINISHED,
};

typedef struct _TorrentServer_Args TorrentServer_Args;
struct _TorrentServer_Args {
	gchar *serverPort;
	gchar *maxConnections;
};

typedef struct _TorrentServer_Connection TorrentServer_Connection;
struct _TorrentServer_Connection {
	gint sockd;
	gint addr;
	enum torrentServer_state state;
	gint downBytesTransfered;
	gint upBytesTransfered;
};

typedef struct _TorrentServer TorrentServer;
struct _TorrentServer {
	gint epolld;
	gint listenSockd;
	/* connections stored by sockd */
	GHashTable *connections;
	gint downBlockSize;
	gint upBlockSize;
	gint errcode;
};

gint torrentServer_start(TorrentServer* ts, gint epolld, in_addr_t listenIP, in_port_t listenPort, gint downBlockSize, gint upBlockSize);
gint torrentServer_activate(TorrentServer* ts, gint sockd, gint events);
gint torrentServer_accept(TorrentServer* ts, gint* sockdOut);
gint torrentServer_shutdown(TorrentServer* ts);

#endif /* SHD_TORRENT_SERVER_H_ */
