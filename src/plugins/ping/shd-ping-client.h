/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
