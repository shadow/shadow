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

#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <arpa/inet.h>

#include "shd-ping-service.h"

static void pingService_log(PingService *pingSvc, enum pingService_loglevel level, const gchar* format, ...) {
	/* if they gave NULL as a callback, dont log */
	if(pingSvc != NULL && pingSvc->log_cb != NULL) {
		va_list vargs, vargs_copy;
		size_t s = sizeof(pingSvc->logBuffer);

		va_start(vargs, format);
		va_copy(vargs_copy, vargs);
		vsnprintf(pingSvc->logBuffer, s, format, vargs);
		va_end(vargs_copy);

		pingSvc->logBuffer[s-1] = '\0';

		(*(pingSvc->log_cb))(level, pingSvc->logBuffer);
	}
}

gint pingService_startNode(PingService *pingSvc, PingService_Args *args, gint serverEpolld, gint clientEpolld) {
	pingSvc->hostbyname_cb = args->hostbyname_cb;
	pingSvc->log_cb = args->log_cb;
	pingSvc->callback_cb = args->callback_cb;

	in_addr_t socksAddr = (*(pingSvc->hostbyname_cb))(args->socksHostname);
	in_port_t socksPort = atoi(args->socksPort);
	gint pingInterval = atoi(args->pingInterval);
	gint pingSize = 64;
	if(args->pingSize) {
		gint pingSize = atoi(args->pingSize);
	}

	/* get IP address of current node */
	gchar myHostname[128];
	gethostname(myHostname, sizeof(myHostname));
	in_addr_t serverAddr = (*(pingSvc->hostbyname_cb))(myHostname);
	in_port_t serverPort = PING_PORT;

	pingSvc->serverEpolld = serverEpolld;
	pingSvc->clientEpolld = clientEpolld;
	pingSvc->server = g_new0(PingServer, 1);
	pingSvc->client = g_new0(PingClient, 1);
	pingSvc->client->createCallback = args->callback_cb;
	pingSvc->pingsTransfered = 0;

	/* start the server that will listen for the ping */
	gint ret = pingServer_start(pingSvc->server, pingSvc->serverEpolld, serverAddr, htons(serverPort));
	if(ret > 0) {
		pingService_log(pingSvc, PING_CRITICAL, "Error %d while starting the ping server", ret);
		return ret;
	}
	pingService_log(pingSvc, PING_NOTICE, "successfully started server on port %d", serverPort);

	/* create client socket so we can connect to socks and/or server */
	ret = pingClient_start(pingSvc->client, pingSvc->clientEpolld, socksAddr, htons(socksPort), serverAddr, htons(serverPort), pingInterval, pingSize);
	if(ret > 0) {
		pingService_log(pingSvc, PING_CRITICAL, "Error %d while starting the ping client", ret);
		return ret;
	}
	pingService_log(pingSvc, PING_NOTICE, "successfully started client [%8.8X]", pingSvc->client->cookie);

	return 0;
}

gint pingService_activate(PingService *pingSvc, gint sockd, gint events, gint epolld) {
	pingService_log(pingSvc, PING_DEBUG, "ping activate called with  sockd %d  events %d  epolld %d", sockd, events, epolld);
	gint ret;
	if(epolld == pingSvc->clientEpolld) {
		ret = pingClient_activate(pingSvc->client, sockd);
		if(ret == PING_CLIENT_ERR_FATAL || ret == PING_CLIENT_ERR_SOCKSCONN) {
			pingService_log(pingSvc, PING_NOTICE, "ping client shutdown with error %d...retrying in 60 seconds", ret);

			pingClient_shutdown(pingSvc->client);
			pingClient_start(pingSvc->client, pingSvc->client->epolld, pingSvc->client->socksAddr, pingSvc->client->socksPort,
					pingSvc->client->serverAddr, pingSvc->client->serverPort, pingSvc->client->pingInterval, pingSvc->client->pingSize);

			/* set wakeup timer and call sleep function */
			struct timespec wakeupTime;
			clock_gettime(CLOCK_REALTIME, &wakeupTime);
			wakeupTime.tv_sec += 60;
			wakeupTime.tv_nsec = 0;
			pingSvc->callback_cb((ShadowPluginCallbackFunc)pingClient_wakeup, pingSvc->client, 60);

			return ret;
		}
	} else {
		pingServer_activate(pingSvc->server, sockd);

		/* check to see if a pingSvc was received and output timing information */
		if(g_list_length(pingSvc->server->pings) > 0) {
			for(GList *iter = pingSvc->server->pings; iter; iter = g_list_next(iter)) {
				PingInfo *info = iter->data;

				if(info->sentTime == pingSvc->lastPingSent && info->recvTime != pingSvc->lastPingRecv) {
					gint64 diff = (info->recvTime - pingSvc->lastPingRecv) - (info->sentTime - pingSvc->lastPingSent);
					pingService_log(pingSvc, PING_NOTICE, "[ping-train] %f ms difference between pings", diff / 1000000.0);
				}

				pingSvc->lastPingSent = info->sentTime;
				pingSvc->lastPingRecv = info->recvTime;

				pingSvc->pingsTransfered++;
				pingService_log(pingSvc, PING_NOTICE, "[%d.%9.9d] [%d.%9.9d] [%8.8X] received ping %d in %f ms",
						info->sentTime / 1000000000, info->sentTime % 1000000000,
						info->recvTime / 1000000000, info->recvTime % 1000000000,
						info->cookie, pingSvc->pingsTransfered, (gdouble)(info->recvTime - info->sentTime) / 1000000.0);
			}

			g_list_free_full(pingSvc->server->pings, g_free);
			pingSvc->server->pings = NULL;
		}
	}

	return ret;
}

gint pingService_stop(PingService *pingSvc) {
	if(pingSvc->client) {
		pingClient_shutdown(pingSvc->client);
		g_free(pingSvc->client);
	}

	if(pingSvc->server) {
		pingServer_shutdown(pingSvc->server);
		g_free(pingSvc->server);
	}

	return  0;
}
