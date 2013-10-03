/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TORRENT_H_
#define SHD_TORRENT_H_

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

#include "shd-ping-server.h"
#include "shd-ping-client.h"

#define MAX_EVENTS 10
#define PING_PORT 25

enum ping_code {
	PING_SUCCESS, PING_BLOCK_DOWNLOADED, PING_CLOSED, PING_ERR_INVALID, PING_ERR_FATAL, PING_ERR_BADSD, PING_ERR_WOULDBLOCK, PING_ERR_BUFSPACE,
	PING_ERR_SOCKET, PING_ERR_BIND, PING_ERR_LISTEN, PING_ERR_ACCEPT, PING_ERR_RECV, PING_ERR_SEND, PING_ERR_CLOSE, PING_ERR_EPOLL, PING_ERR_CONNECT,
	PING_ERR_SOCKSINIT, PING_ERR_SOCKSCONN, PING_ERR_NOSERVER,
};

#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

typedef struct _Ping Ping;
struct _Ping {
	ShadowFunctionTable* shadowlib;
	gint epolld;
	PingServer *server;
	PingClient *client;
	gint pingsTransfered;
};

Ping**  ping_init(Ping* currentPing);
void ping_new(int argc, char* argv[]);
void ping_activate();
void ping_free();


#endif /* SHD_TORRENT_H_ */
