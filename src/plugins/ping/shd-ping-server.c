/**
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
#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <assert.h>
#include <event.h>

#include <string.h>

#include "shd-ping-server.h"

#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

#define PING_SERVER_ASSERTIO(ping, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return PING_SERVER_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			fprintf(stderr, "ping fatal error: %s\n", strerror(errno)); \
			return PING_SERVER_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		return PING_SERVER_ERR_FATAL; \
	}

void pingServer_changeEpoll(gint epolld, gint sockd, gint event) {
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = sockd;
	epoll_ctl(epolld, EPOLL_CTL_MOD, sockd, &ev);
}

gint pingServer_start(PingServer *pingServer, gint epolld, in_addr_t serverAddr, in_addr_t serverPort) {
	/* create the socket and get a socket descriptor */
	gint sockd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if (sockd == -1) {
		return PING_SERVER_ERR_SOCKET;
	}

	/* setup the socket address info, client has outgoing connection to server */
	struct sockaddr_in listener;
	memset(&listener, 0, sizeof(listener));
	listener.sin_family = AF_INET;
	listener.sin_addr.s_addr = serverAddr;
	listener.sin_port = serverPort;

	/* bind the socket to the server port */
	gint result = bind(sockd, (struct sockaddr *) &listener, sizeof(listener));
	if (result == -1) {
		return PING_SERVER_ERR_BIND;
	}

	/* set as server socket that will listen for clients */
	result = listen(sockd, 10);
	if (result == -1) {
		return PING_SERVER_ERR_LISTEN;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockd;
	if(epoll_ctl(epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return PING_SERVER_ERR_EPOLL;
	}

	pingServer->sockd = sockd;
	pingServer->epolld = epolld;

	return PING_SERVER_SUCCESS;
}

gint pingServer_accept(PingServer *pingServer, gint* sockdOut) {
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	/* try to accept a connection */
	gint sockd = accept(pingServer->sockd, (struct sockaddr *)(&addr), &addrlen);
	if(sockd < 0) {
		return PING_SERVER_ERR_ACCEPT;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockd;
	if(epoll_ctl(pingServer->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return PING_SERVER_ERR_EPOLL;
	}

	if(sockdOut != NULL) {
		*sockdOut = sockd;
	}

	return 0;
}

gint pingServer_activate(PingServer *pingServer, gint sockd) {
	if(sockd == pingServer->sockd) {
		gint ret = 0;
		while(!ret) {
			ret = pingServer_accept(pingServer, NULL);
		}
		return ret;
	}

	gchar buf[128];
	gint bytes = recv(sockd, buf, sizeof(buf), 0);
	PING_SERVER_ASSERTIO(pingServer, bytes, errno == EWOULDBLOCK, PING_SERVER_ERR_RECV);

	if(bytes > 0) {
		buf[bytes] = 0;
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		gint32 cookie = 0;
		gint64 sentTime = 0;

		gchar **parts = g_strsplit(buf, "\r\n", 0);
		for(gint idx = 0; parts[idx]; idx++) {
			if(!g_strcmp0(parts[idx], "")) {
				if(sentTime != 0 && cookie != 0) {
					PingInfo *info = g_new0(PingInfo, 1);
					info->cookie = cookie;
					info->sentTime = sentTime;
					info->recvTime = TIME_TO_NS(now);
					pingServer->pings = g_list_append(pingServer->pings, info);
				}
			} else {
				gchar **data = g_strsplit(parts[idx], ": ", 2);
				if(!g_strcmp0(data[0], "TOR-COOKIE") && data[1]) {
					cookie = g_ascii_strtoull(data[1], NULL, 16);
				} else if(!g_strcmp0(data[0], "TIME") && data[1]) {
					sentTime = g_ascii_strtoull(data[1], NULL, 10);
				}
				g_strfreev(data);
			}
		}
		g_strfreev(parts);
	}


	return PING_SERVER_SUCCESS;
}

gint pingServer_shutdown(PingServer *pingServer) {
	epoll_ctl(pingServer->epolld, EPOLL_CTL_DEL, pingServer->sockd, NULL);
	close(pingServer->sockd);
	pingServer->sockd = 0;

	g_list_free_full(pingServer->pings, g_free);

	return PING_SERVER_SUCCESS;
}
