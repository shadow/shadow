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

#ifndef SHD_PING_CLIENT_H_
#define SHD_PING_CLIENT_H_

#include <glib.h>
#include <shd-library.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <event.h>

#define MAX_EVENTS 10

enum ping_client_code {
	PING_CLIENT_SUCCESS, PING_CLIENT_BLOCK_DOWNLOADED, PING_CLIENT_CLOSED, PING_CLIENT_ERR_INVALID, PING_CLIENT_ERR_FATAL, PING_CLIENT_ERR_BADSD, PING_CLIENT_ERR_WOULDBLOCK, PING_CLIENT_ERR_BUFSPACE,
	PING_CLIENT_ERR_SOCKET, PING_CLIENT_ERR_BIND, PING_CLIENT_ERR_LISTEN, PING_CLIENT_ERR_ACCEPT, PING_CLIENT_ERR_RECV, PING_CLIENT_ERR_SEND, PING_CLIENT_ERR_CLOSE, PING_CLIENT_ERR_EPOLL, PING_CLIENT_ERR_CONNECT,
	PING_CLIENT_ERR_SOCKSINIT, PING_CLIENT_ERR_SOCKSCONN, PING_CLIENT_ERR_NOSERVER,
};

enum PingClient_State {
	PING_CLIENT_SOCKS_REQUEST_INIT,
	PING_CLIENT_SOCKS_TOREPLY_INIT,
	PING_CLIENT_SOCKS_REPLY_INIT,
	PING_CLIENT_SOCKS_REQUEST_CONN,
	PING_CLIENT_SOCKS_TOREPLY_CONN,
	PING_CLIENT_SOCKS_REPLY_CONN,
	PING_CLIENT_SEND,
	PING_CLIENT_RECEIVE,
	PING_CLIENT_PING,
	PING_CLIENT_IDLE,
};

#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

/* version 5, one supported auth method, no auth */
#define PING_CLIENT_SOCKS_INIT "\x05\x01\x00"
#define PING_CLIENT_SOCKS_INIT_LEN 3
/* version 5, auth choice (\xFF means none supported) */
#define PING_CLIENT_SOCKS_CHOICE "\x05\x01"
#define PING_CLIENT_SOCKS_CHOICE_LEN 2
/* v5, TCP conn, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define PING_CLIENT_SOCKS_REQ_HEAD "\x05\x01\x00\x01"
#define PING_CLIENT_SOCKS_REQ_HEAD_LEN 4
/* v5, status, reserved, IPV4, ip_addr (4 bytes), port (2 bytes) */
#define PING_CLIENT_SOCKS_RESP_HEAD "\x05\x00\x00\x01"
#define PING_CLIENT_SOCKS_RESP_HEAD_LEN 4

#define PING_CLIENT_BUF_SIZE 16384

typedef void (*pingClient_createCallback_cb)(void *func, void *data, guint milliseconds);

typedef struct _PingClient PingClient;
struct _PingClient {
	gint epolld;
	gint sockd;

	in_addr_t socksAddr;
	in_port_t socksPort;
	in_addr_t serverAddr;
	in_port_t serverPort;
	GQueue *pingTimes;
	gint pingInterval;
	gint pingSize;
	gint pingsSent;
	guint32 cookie;

	pingClient_createCallback_cb createCallback;

	enum PingClient_State clientState;
	enum PingClient_State clientNextstate;
	gchar buf[PING_CLIENT_BUF_SIZE];
	size_t buf_write_offset;
	size_t buf_read_offset;
};

gint pingClient_start(PingClient *pingClient, gint epolld, in_addr_t socksAddr, in_port_t socksPort, in_addr_t serverAddr, in_port_t serverPort,
		gint pingInterval, gint pingSize);
gint pingClient_activate(PingClient *pingClient, gint sockd);
void pingClient_wakeup(PingClient *pingClient);
gint pingClient_shutdown(PingClient *pingClient);


#endif /* SHD_PING_CLIENT_H_ */
