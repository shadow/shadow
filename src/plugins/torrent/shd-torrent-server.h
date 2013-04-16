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
	TS_ERR_SOCKET, TS_ERR_CONNECT, TS_ERR_BIND, TS_ERR_LISTEN, TS_ERR_ACCEPT, TS_ERR_RECV, TS_ERR_SEND, TS_ERR_CLOSE, TS_ERR_EPOLL, TS_ERR_NOCONN
};

enum torrentServer_state {
	TS_IDLE,
	TS_AUTH_REGISTER,
	TS_AUTH_IDLE,
	TS_REQUEST,
	TS_TRANSFER,
	TS_FINISHED,
};

typedef struct _TorrentServer_Args TorrentServer_Args;
struct _TorrentServer_Args {
	gchar *serverPort;
	gchar *maxConnections;
};

typedef struct _TorrentServer_PacketInfo TorrentServer_PacketInfo ;
struct _TorrentServer_PacketInfo {
	gint64 sendTime;
	gint64 recvTime;
	guint32 cookie;
};


typedef struct _TorrentServer_Connection TorrentServer_Connection;
struct _TorrentServer_Connection {
	gint sockd;
	gint addr;
	enum torrentServer_state state;
	gint downBytesTransfered;
	gint upBytesTransfered;
	guint32 cookie;
};

typedef struct _TorrentServer TorrentServer;
struct _TorrentServer {
	gint epolld;
	gint listenSockd;
	gint authSockd;
	in_port_t serverPort;
	GHashTable *connections;
	gint downBlockSize;
	gint upBlockSize;
	gint errcode;
	GQueue *packetInfo;
};

gint torrentServer_start(TorrentServer* ts, gint epolld, in_addr_t listenAddr, in_port_t listenPort,
		in_addr_t authAddr, in_port_t authPort, gint downBlockSize, gint upBlockSize);
gint torrentServer_activate(TorrentServer* ts, gint sockd, gint events);
gint torrentServer_accept(TorrentServer* ts, gint* sockdOut);
gint torrentServer_shutdown(TorrentServer* ts);

#endif /* SHD_TORRENT_SERVER_H_ */
