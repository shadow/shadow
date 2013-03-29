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
