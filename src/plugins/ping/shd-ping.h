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
