/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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
#include <netinet/in.h>
#include <sys/epoll.h>

#include "shd-echo.h"

void mylog(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
	va_list variableArguments;
	va_start(variableArguments, format);
	g_logv(G_LOG_DOMAIN, level, format, variableArguments);
	va_end(variableArguments);
}

gint main(gint argc, gchar *argv[]) {
	Echo echo;

	mylog(G_LOG_LEVEL_DEBUG, __FUNCTION__, "Starting echo program");

	echo.client = NULL;
	echo.server = NULL;

	char* USAGE = "Echo usage: 'client serverIP', 'server', or 'loopback'";
	if(argc < 2) {
		mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
		return -1;
	}

	/* parse command line args */
	char* mode = argv[1];

	if(g_strcasecmp(mode, "client") == 0) {
		if(argc < 3) {
			mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
			return -1;
		}

		/* start up a client, connecting to the server specified in args */
		char* serverIPString = argv[2];
		struct in_addr in;
		gint is_ip_address = inet_aton(serverIPString, &in);
		if(!is_ip_address) {
			mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
			return -1;
		}
		in_addr_t serverIP = in.s_addr;
		echo.client = echoclient_new(serverIP, mylog);
	} else if(g_strcasecmp(mode, "server") == 0) {
		in_addr_t serverIP = INADDR_ANY;
		echo.server = echoserver_new(serverIP, mylog);
	} else if(g_strcasecmp(mode, "loopback") == 0) {
		echo.server = echoserver_new(htonl(INADDR_LOOPBACK), mylog);
		echo.client = echoclient_new(htonl(INADDR_LOOPBACK), mylog);
	} else {
		mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
		return -1;
	}

	/* do an epoll on the client/server epoll descriptors, so we know when
	 * we can wait on either of them without blocking.
	 */
	gint epolld = 0;
	if((epolld = epoll_create(1)) == -1) {
		mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		return -1;
	} else {
		if(echo.server) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = echo.server->epollFileDescriptor;
			if(epoll_ctl(epolld, EPOLL_CTL_ADD, echo.server->epollFileDescriptor, &ev) == -1) {
				mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				return -1;
			}
		}
		if(echo.client) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = echo.client->epollFileDescriptor;
			if(epoll_ctl(epolld, EPOLL_CTL_ADD, echo.client->epollFileDescriptor, &ev) == -1) {
				mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				return -1;
			}
		}
	}

	/* main loop - when the client/server epoll fds are ready, activate them */
	for(;;) {
		struct epoll_event events[10];
		int nfds = epoll_wait(epolld, events, 10, -1);
		if(nfds == -1) {
			mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
		}

		for(int i = 0; i < nfds; i++) {
			if(events[i].events & EPOLLIN) {
				if(echo.server && events[i].data.fd == echo.server->epollFileDescriptor) {
					echoserver_ready(echo.server, mylog);
				}
				if(echo.client && events[i].data.fd == echo.client->epollFileDescriptor) {
					echoclient_ready(echo.client, mylog);
				}
			}
		}

		if(echo.client && echo.client->is_done) {
			close(echo.client->sd);
			echoclient_free(echo.client);
			break;
		}
	}

	return 0;
}
