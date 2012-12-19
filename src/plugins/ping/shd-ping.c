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

#include "shd-ping.h"

Ping* ping;

#define PING_ASSERTIO(ping, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return PING_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			fprintf(stderr, "ping fatal error: %s\n", strerror(errno)); \
			return PING_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		return PING_ERR_FATAL; \
	}

static in_addr_t ping_resolveHostname(const gchar* hostname) {
	ShadowLogFunc log = ping->shadowlib->log;
	in_addr_t addr = 0;

	/* get the address in network order */
	if(g_ascii_strncasecmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(g_ascii_strncasecmp(hostname, "localhost", 9) == 0) {
		addr = htonl(INADDR_LOOPBACK);
	} else {
		struct addrinfo* info;
		int ret = getaddrinfo((gchar*) hostname, NULL, NULL, &info);
		if(ret >= 0) {
			addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		} else {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
	}

	return addr;
}

Ping**  ping_init(Ping* currentPing) {
	ping = currentPing;
	return &ping;
}

void ping_new(int argc, char* argv[]) {
	ShadowLogFunc log = ping->shadowlib->log;
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "ping_new called");

	const gchar* USAGE = "Ping USAGE: socksHostname socksPort pingInterval [pingSize (default=64)]";
	if(argc < 4) {
		log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
		return;
	}

	in_addr_t socksAddr = ping_resolveHostname(argv[1]);
	in_port_t socksPort = atoi(argv[2]);
	gint pingInterval = atoi(argv[3]);
	gint pingSize = 64;
	if(argc > 4) {
		pingSize = atoi(argv[4]);
	}

	/* get IP address of current node */
	gchar myHostname[128];
	gethostname(myHostname, sizeof(myHostname));
	in_addr_t serverAddr = ping_resolveHostname(myHostname);
	in_port_t serverPort = PING_PORT;

	/* create an epoll to wait for I/O events */
	ping->epolld = epoll_create(1);
	if(ping->epolld == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(ping->epolld);
		ping->epolld = 0;
		return;
	}

	int ret = 0;
	ping->server = g_new0(PingServer, 1);
	ping->client = g_new0(PingClient, 1);
	ping->client->createCallback = (pingClient_createCallback_cb) ping->shadowlib->createCallback;
	ping->pingsTransfered = 0;

	/* start the server that will listen for the ping */
	ret = pingServer_start(ping->server, ping->epolld, serverAddr, serverPort);
	if(ret < 0) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error while starting the ping server");
		return;
	}
	log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully started server on port %d", serverPort);

	/* create client socket so we can connect to socks and/or server */
	ret = pingClient_start(ping->client, ping->epolld, socksAddr, socksPort, serverAddr, serverPort, pingInterval, pingSize);
	if(ret < 0) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error while starting the ping client");
		return;
	}
	log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully started client [%8.8X]", ping->client->cookie);
}


void ping_activate() {
	ShadowLogFunc log = ping->shadowlib->log;

	struct epoll_event events[10];
	int nfds = epoll_wait(ping->epolld, events, 10, 0);
	if(nfds == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
		return;
	}

	for(int i = 0; i < nfds; i++) {
		gint sockd = events[i].data.fd;

		if(sockd == ping->client->sockd) {
			pingClient_activate(ping->client, sockd);
		} else {
			pingServer_activate(ping->server, sockd);

			/* check to see if a pingSvc was received and output timing information */
			if(g_list_length(ping->server->pings) > 0) {
				for(GList *iter = ping->server->pings; iter; iter = g_list_next(iter)) {
					PingInfo *info = iter->data;

					ping->pingsTransfered++;
					log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d.%9.9d] [%8.8X] received ping %d in %f ms", info->sentTime / 1000000000, info->sentTime % 1000000000,
							info->cookie, ping->pingsTransfered, (gdouble)(info->recvTime - info->sentTime) / 1000000.0);
				}

				g_list_free_full(ping->server->pings, g_free);
				ping->server->pings = NULL;
			}
		}
	}
}

void ping_free() {
	if(ping->client) {
		pingClient_shutdown(ping->client);
		g_free(ping->client);
	}

	if(ping->server) {
		pingServer_shutdown(ping->server);
		g_free(ping->server);
	}
}
