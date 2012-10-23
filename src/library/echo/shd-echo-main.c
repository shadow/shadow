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
	Echo echostate;
	memset(&echostate, 0, sizeof(Echo));

	mylog(G_LOG_LEVEL_DEBUG, __FUNCTION__, "Starting echo program");

	const char* USAGE = "Echo USAGE: 'tcp client serverIP', 'tcp server', 'tcp loopback', 'tcp socketpair', "
			"'udp client serverIP', 'udp server', 'udp loopback', 'pipe'\n"
			"** clients and servers must be paired together, but loopback, socketpair,"
			"and pipe modes stand on their own.";

	/* 0 is the plugin name, 1 is the protocol */
	if(argc < 2) {
		mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
		return -1;
	}

	char* protocol = argv[1];

	gboolean isError = TRUE;

	/* check for the protocol option and create the correct application state */
	if(g_ascii_strncasecmp(protocol, "tcp", 3) == 0)
	{
		echostate.protocol = ECHOP_TCP;
		echostate.etcp = echotcp_new(mylog, argc - 2, &argv[2]);
		isError = (echostate.etcp == NULL) ? TRUE : FALSE;
	}
	else if(g_ascii_strncasecmp(protocol, "udp", 3) == 0)
	{
		echostate.protocol = ECHOP_UDP;
		echostate.eudp = echoudp_new(mylog, argc - 2, &argv[2]);
		isError = (echostate.eudp == NULL) ? TRUE : FALSE;
	}
	else if(g_ascii_strncasecmp(protocol, "pipe", 4) == 0)
	{
		echostate.protocol = ECHOP_PIPE;
		echostate.epipe = echopipe_new(mylog);
		isError = (echostate.epipe == NULL) ? TRUE : FALSE;
	}

	if(isError) {
		/* unknown argument for protocol, log usage information through Shadow */
		mylog(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
	}

	EchoServer* server = echostate.etcp ? echostate.etcp->server : echostate.eudp ? echostate.eudp->server : NULL;
	EchoClient* client = echostate.etcp ? echostate.etcp->client : echostate.eudp ? echostate.eudp->client : NULL;
	EchoPipe* epipe = echostate.epipe;

	/* do an epoll on the client/server epoll descriptors, so we know when
	 * we can wait on either of them without blocking.
	 */
	gint epolld = 0;
	if((epolld = epoll_create(1)) == -1) {
		mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		return -1;
	} else {
		if(server) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = server->epolld;
			if(epoll_ctl(epolld, EPOLL_CTL_ADD, server->epolld, &ev) == -1) {
				mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				return -1;
			}
		}
		if(client) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = client->epolld;
			if(epoll_ctl(epolld, EPOLL_CTL_ADD, client->epolld, &ev) == -1) {
				mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				return -1;
			}
		}
		if(epipe) {
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = epipe->epolld;
			if(epoll_ctl(epolld, EPOLL_CTL_ADD, epipe->epolld, &ev) == -1) {
				mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				return -1;
			}
		}
	}

	/* main loop - when the client/server epoll fds are ready, activate them */
	while(1) {
		struct epoll_event events[10];
		int nfds = epoll_wait(epolld, events, 10, -1);
		if(nfds == -1) {
			mylog(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
		}

		for(int i = 0; i < nfds; i++) {
			if(events[i].events & EPOLLIN) {
				if(echostate.etcp) {
					echotcp_ready(echostate.etcp);
				}else if(echostate.eudp) {
					echoudp_ready(echostate.eudp);
				} else if(echostate.epipe) {
					echopipe_ready(echostate.epipe);
				}
			}
		}

		if(client && client->is_done) {
			close(client->socketd);
			if(echostate.etcp) {
				echotcp_free(echostate.etcp);
			}
			if(echostate.eudp) {
				echoudp_free(echostate.eudp);
			}
			break;
		}

		if(epipe && epipe->didRead) {
			close(epipe->readfd);
			close(epipe->writefd);
			close(epipe->epolld);
			break;
		}
	}

	return 0;
}
