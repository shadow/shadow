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

#ifndef SHD_TORRENT_CLIENT_H_
#define SHD_TORRENT_CLIENT_H_

#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>
#include "shd-torrent-server.h"

/* version 5, one supported auth method, no auth */
#define TC_SOCKS_INIT "\x05\x01\x00"
#define TC_SOCKS_INIT_LEN 3
/* version 5, auth choice (\xFF means none supported) */
#define TC_SOCKS_CHOICE "\x05\x01"
#define TC_SOCKS_CHOICE_LEN 2
/* v5, TCP conn, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define TC_SOCKS_REQ_HEAD "\x05\x01\x00\x01"
#define TC_SOCKS_REQ_HEAD_LEN 4
/* v5, status, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define TC_SOCKS_RESP_HEAD "\x05\x00\x00\x01"
#define TC_SOCKS_RESP_HEAD_LEN 4

#define TC_BUF_FILLER "TOR-COOKIE: %8.8X\r\nTIME: %lu\r\n"

#define TC_BUF_SIZE 16384

enum torrentClient_code {
	TC_SUCCESS, TC_BLOCK_DOWNLOADED, TC_CLOSED, TC_ERR_INVALID, TC_ERR_FATAL, TC_ERR_BADSD, TC_ERR_WOULDBLOCK, TC_ERR_BUFSPACE,
	TC_ERR_SOCKET, TC_ERR_BIND, TC_ERR_LISTEN, TC_ERR_ACCEPT, TC_ERR_RECV, TC_ERR_SEND, TC_ERR_CLOSE, TC_ERR_EPOLL, TC_ERR_CONNECT,
	TC_ERR_SOCKSINIT, TC_ERR_SOCKSCONN, TC_ERR_NOSERVER,
};

enum TorrentClient_State {
	TC_SOCKS_REQUEST_INIT,
	TC_SOCKS_TOREPLY_INIT,
	TC_SOCKS_REPLY_INIT,
	TC_SOCKS_REQUEST_CONN,
	TC_SOCKS_TOREPLY_CONN,
	TC_SOCKS_REPLY_CONN,
	TC_SEND,
	TC_RECEIVE,
	TC_AUTH_REQUEST_NODES,
	TC_AUTH_RECEIVE_NODES,
	TC_AUTH_IDLE,
	TC_SERVER_REQUEST,
	TC_SERVER_TRANSFER,
	TC_SERVER_FINISHED,
	TC_SERVER_IDLE,
	TC_SERVER_INVALID,
};

enum torrentClient_loglevel {
	TC_CRITICAL, TC_WARNING, TC_NOTICE, TC_INFO, TC_DEBUG
};

typedef void (*torrentClient_log_cb)(enum torrentClient_loglevel level, const gchar* message);

typedef struct _TorrentClient_Args TorrentClient_Args;
struct _TorrentClient_Args {
	gchar *socksHostname;
	gchar *socksPort;
	gchar *authHostname;
	gchar *authPort;
	gchar *maxConnections;
};

typedef struct _TorrentClient_Server TorrentClient_Server;
struct _TorrentClient_Server {
	in_addr_t addr;
	in_port_t port;
	gint sockd;
	enum TorrentClient_State state;
	enum TorrentClient_State nextstate;
	gchar buf[TC_BUF_SIZE];
	size_t buf_write_offset;
	size_t buf_read_offset;

	guint32 cookie;

	gint downBytesTransfered;
	gint upBytesTransfered;

	struct timespec download_start;
	struct timespec download_first_byte;
	struct timespec download_end;
};

typedef struct _TorrentClient TorrentClient;
struct _TorrentClient {
	gint epolld;
	gint sockd;
	gint authSockd;
	in_port_t serverPort;
	gint maxConnections;
	GList *servers;
	GHashTable *connections;
	struct timespec lastServerListFetch;

	in_addr_t socksAddr;
	in_port_t socksPort;
	in_addr_t authAddr;
	in_port_t authPort;
	gint errcode;

	gint totalBytesDown;
	gint totalBytesUp;
	gint bytesInProgress;
	gint fileSize;
	gint downBlockSize;
	gint upBlockSize;
	gint blocksDownloaded;
	gint blocksRemaining;
	gint numBlocks;

	struct timespec download_start;
	struct timespec download_first_byte;
	struct timespec download_end;

	TorrentClient_Server *currentBlockTransfer;

	torrentClient_log_cb log_cb;
	gchar logBuffer[1024];
};

gint torrentClient_start(TorrentClient* tc, gint epolld, in_addr_t socksAddr, in_port_t socksPort, in_addr_t authAddr, in_port_t authPort,
		in_port_t serverPort, gint fileSize, gint downBlockSize, gint upBlockSize);
gint torrentClient_activate(TorrentClient* tc, gint sockd, gint events);
gint torrentClient_shutdown(TorrentClient* tc);
gint torrentClient_connect(TorrentClient *tc, in_addr_t addr, in_port_t port);

#endif /* SHD_TORRENT_CLIENT_H_ */
