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

#ifndef SHD_PING_SERVER_H_
#define SHD_PING_SERVER_H_

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

enum ping_server_code {
	PING_SERVER_SUCCESS, PING_SERVER_BLOCK_DOWNLOADED, PING_SERVER_CLOSED, PING_SERVER_ERR_INVALID, PING_SERVER_ERR_FATAL, PING_SERVER_ERR_BADSD, PING_SERVER_ERR_WOULDBLOCK, PING_SERVER_ERR_BUFSPACE,
	PING_SERVER_ERR_SOCKET, PING_SERVER_ERR_BIND, PING_SERVER_ERR_LISTEN, PING_SERVER_ERR_ACCEPT, PING_SERVER_ERR_RECV, PING_SERVER_ERR_SEND, PING_SERVER_ERR_CLOSE, PING_SERVER_ERR_EPOLL, PING_SERVER_ERR_CONNECT,
	PING_SERVER_ERR_SOCKSINIT, PING_SERVER_ERR_SOCKSCONN, PING_SERVER_ERR_NOSERVER,
};

typedef struct _PingInfo PingInfo;
struct _PingInfo {
	guint64 sentTime, recvTime;
	guint32 cookie;
};

typedef struct _PingServer PingServer;
struct _PingServer {
	gint sockd;
	gint epolld;
	GList *pings;

	//gint errno;
	gchar *errmsg;
};

gint pingServer_start(PingServer *pingServer, gint epolld, in_addr_t serverAddr, in_addr_t serverPort);
gint pingServer_activate(PingServer *pingServer, gint sockd);
gint pingServer_shutdown(PingServer *pingServer);


#endif /* SHD_PING_SERVER_H_ */
