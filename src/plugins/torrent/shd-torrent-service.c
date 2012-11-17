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

#include "shd-torrent-service.h"

static void torrentService_log(TorrentService *tsvc, enum torrentService_loglevel level, const gchar* format, ...) {
	/* if they gave NULL as a callback, dont log */
	if(tsvc != NULL && tsvc->log_cb != NULL) {
		va_list vargs, vargs_copy;
		size_t s = sizeof(tsvc->logBuffer);

		va_start(vargs, format);
		va_copy(vargs_copy, vargs);
		vsnprintf(tsvc->logBuffer, s, format, vargs);
		va_end(vargs_copy);

		tsvc->logBuffer[s-1] = '\0';

		(*(tsvc->log_cb))(level, tsvc->logBuffer);
	}
}

static void torrentService_report(TorrentService* tsvc, gchar* preamble) {
	if(tsvc != NULL && tsvc->client != NULL && preamble != NULL) {
		TorrentClient *tc = tsvc->client;
		struct timespec now;
		struct timespec first_time;
		struct timespec curr_time;
		struct timespec block_first_time;
		struct timespec block_curr_time;
		clock_gettime(CLOCK_REALTIME, &now);

		/* first byte statistics */
		first_time.tv_sec = tc->download_first_byte.tv_sec - tc->download_start.tv_sec;
		first_time.tv_nsec = tc->download_first_byte.tv_nsec - tc->download_start.tv_nsec;
		while(first_time.tv_nsec < 0) {
			first_time.tv_sec--;
			first_time.tv_nsec += 1000000000;
		}

		/* current byte statistics */
		curr_time.tv_sec = now.tv_sec - tc->download_start.tv_sec;
		curr_time.tv_nsec = now.tv_nsec - tc->download_start.tv_nsec;
		while(curr_time.tv_nsec < 0) {
			curr_time.tv_sec--;
			curr_time.tv_nsec += 1000000000;
		}

		/* first byte statistics */
		block_first_time.tv_sec = tc->currentBlockTransfer->download_first_byte.tv_sec - tc->currentBlockTransfer->download_start.tv_sec;
		block_first_time.tv_nsec = tc->currentBlockTransfer->download_first_byte.tv_nsec - tc->currentBlockTransfer->download_start.tv_nsec;
		while(block_first_time.tv_nsec < 0) {
			block_first_time.tv_sec--;
			block_first_time.tv_nsec += 1000000000;
		}

		/* current byte statistics */
		block_curr_time.tv_sec = now.tv_sec - tc->currentBlockTransfer->download_start.tv_sec;
		block_curr_time.tv_nsec = now.tv_nsec - tc->currentBlockTransfer->download_start.tv_nsec;
		while(block_curr_time.tv_nsec < 0) {
			block_curr_time.tv_sec--;
			block_curr_time.tv_nsec += 1000000000;
		}


		torrentService_log(tsvc, TSVC_NOTICE, "%s first byte in %lu.%.3d seconds, "
				"%d of %d DOWN and %d of %d UP in %lu.%.3d seconds, total %d of %d bytes [%d\%] in %lu.%.3d seconds (block %d of %d)",
						preamble,
						block_first_time.tv_sec, (gint)(block_first_time.tv_nsec / 1000000),
						tc->currentBlockTransfer->downBytesTransfered, tc->downBlockSize,
						tc->currentBlockTransfer->upBytesTransfered, tc->upBlockSize,
						block_curr_time.tv_sec, (gint)(block_curr_time.tv_nsec / 1000000),
						tc->totalBytesDown, tc->fileSize, (gint)(((gdouble)tc->totalBytesDown / (gdouble)tc->fileSize) * 100),
						curr_time.tv_sec, (gint)(curr_time.tv_nsec / 1000000),
						tc->blocksDownloaded, tc->numBlocks);
	}
}

int torrentService_startNode(TorrentService *tsvc, TorrentService_NodeArgs *args, gint serverEpolld, gint clientEpolld, gint* sockd_out) {
	assert(tsvc);
	assert(args);

	gchar* nodeType = args->nodeType;
	gchar* authorityHostname = args->authorityHostname;
	gint authorityPort = atoi(args->authorityPort);
	gchar* socksHostname = args->socksHostname;
	gint socksPort = atoi(args->socksPort);
	gint serverPort = atoi(args->serverPort);
	gint fileSize = 0;
	if(strstr(args->fileSize, "KB") != NULL) {
		fileSize = atoi(strtok(args->fileSize, "K")) * 1024;
	} else if(strstr(args->fileSize, "MB") != NULL) {
		fileSize = atoi(strtok(args->fileSize, "M")) * 1024 * 1024;
	} else {
		fileSize = atoi(args->fileSize);
	}

	gint downBlockSize = 16*1024;
	if(args->downBlockSize) {
		if(strstr(args->downBlockSize, "KB") != NULL) {
			downBlockSize = atoi(strtok(args->downBlockSize, "K")) * 1024;
		} else if(strstr(args->downBlockSize, "MB") != NULL) {
			downBlockSize = atoi(strtok(args->downBlockSize, "M")) * 1024 * 1024;
		} else {
			downBlockSize = atoi(args->downBlockSize);
		}
	}

	gint upBlockSize = 16*1024;
	if(args->upBlockSize) {
		if(strstr(args->upBlockSize, "KB") != NULL) {
			upBlockSize = atoi(strtok(args->upBlockSize, "K")) * 1024;
		} else if(strstr(args->upBlockSize, "MB") != NULL) {
			upBlockSize = atoi(strtok(args->upBlockSize, "M")) * 1024 * 1024;
		} else {
			upBlockSize = atoi(args->upBlockSize);
		}
	}

	tsvc->log_cb = args->log_cb;
	tsvc->hostbyname_cb = args->hostbyname_cb;
	tsvc->sleep_cb = args->sleep_cb;

	torrentService_log(tsvc, TSVC_NOTICE, "attempting to start torrent node of type '%s'", nodeType);

	in_addr_t authAddr = (*(tsvc->hostbyname_cb))(authorityHostname);
	if(!g_ascii_strncasecmp(nodeType, "server", 6) || !g_ascii_strncasecmp(nodeType, "node", 4)) {
		/* start server to listen for connections */
		in_addr_t listenIP = INADDR_ANY;
		in_port_t listenPort = (in_port_t)serverPort;

		tsvc->server = g_new0(TorrentServer, 1);
		// NOTE: since the up/down block sizes are in context of the client, we swap them for
		// the server since it's actually the reverse of what the client has
		int ret = torrentServer_start(tsvc->server, serverEpolld, htonl(listenIP), htons(listenPort), authAddr, htons(authorityPort), upBlockSize, downBlockSize);
		if(ret < 0) {
			torrentService_log(tsvc, TSVC_WARNING, "torrent server error, not started");
			g_free(tsvc->server);
			tsvc->server = NULL;
			return -1;
		} else {
			torrentService_log(tsvc, TSVC_NOTICE, "torrent server running on at %s:%u", inet_ntoa((struct in_addr){listenIP}), listenPort);
		}
	}

	if(!g_ascii_strncasecmp(nodeType, "client", 6) || !g_ascii_strncasecmp(nodeType, "node", 4)) {
		/* start up client */
		in_addr_t socksAddr = (*(tsvc->hostbyname_cb))(socksHostname);
		in_addr_t authAddr = (*(tsvc->hostbyname_cb))(authorityHostname);

		tsvc->client = g_new0(TorrentClient, 1);
		if(torrentClient_start(tsvc->client, clientEpolld, socksAddr, htons(socksPort), authAddr, htons(authorityPort), serverPort,
				fileSize, downBlockSize, upBlockSize) < 0) {
			torrentService_log(tsvc, TSVC_WARNING, "torrent client error, not started!");
			g_free(tsvc->client);
			tsvc->client = NULL;
			return -1;
		} else {
			torrentService_log(tsvc, TSVC_NOTICE, "torrent client running");
		}
		tsvc->client->log_cb = args->log_cb;
	}

	return 0;
}

int torrentService_activate(TorrentService *tsvc, gint sockd, gint events, gint epolld) {
	if(tsvc->client && tsvc->client->epolld == epolld) {
		gint ret = torrentClient_activate(tsvc->client, sockd, events);

		 if(ret == TC_ERR_FATAL || ret == TC_ERR_SOCKSCONN) {
			torrentService_log(tsvc, TSVC_NOTICE, "torrent client shutdown with error %d...retrying in 60 seconds", ret);

			torrentClient_shutdown(tsvc->client);
			torrentClient_start(tsvc->client, tsvc->client->epolld, tsvc->client->socksAddr, tsvc->client->socksPort,
					tsvc->client->authAddr, tsvc->client->authPort, tsvc->client->serverPort, tsvc->client->fileSize,
					tsvc->client->downBlockSize, tsvc->client->upBlockSize);

			/* set wakup timer and call sleep function */
			clock_gettime(CLOCK_REALTIME, &tsvc->wakeupTime);
			tsvc->wakeupTime.tv_sec += 60;
			tsvc->wakeupTime.tv_nsec = 0;
			(*tsvc->sleep_cb)(tsvc, 60);

			return -1;
		} else if(ret != TC_SUCCESS && ret != TC_BLOCK_DOWNLOADED && ret != TC_ERR_RECV && ret != TC_ERR_SEND) {
			torrentService_log(tsvc, TSVC_INFO, "torrent client encountered a "
					"non-asynch-io related error");
		}

		if(!tsvc->clientDone && tsvc->client && tsvc->client->totalBytesDown > 0) {
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			if(tsvc->client->totalBytesDown >= tsvc->client->fileSize) {
				torrentService_report(tsvc, "[client-complete]");
				tsvc->clientDone = 1;
			} else if(ret == TC_BLOCK_DOWNLOADED) {
				tsvc->lastReport = now;
				torrentService_report(tsvc, "[client-block-complete]");
			} else if(now.tv_sec - tsvc->lastReport.tv_sec > 1 && tsvc->client->currentBlockTransfer != NULL &&
					  (tsvc->client->currentBlockTransfer->downBytesTransfered > 0 ||
					   tsvc->client->currentBlockTransfer->upBytesTransfered > 0)) {
				tsvc->lastReport = now;
				torrentService_report(tsvc, "[client-block-progress]");
			}
		}
	}

	if(tsvc->server && tsvc->server->epolld == epolld) {
		torrentServer_activate(tsvc->server, sockd, events);
	}

	return 0;
}

int torrentService_stop(TorrentService *tsvc) {
	if(tsvc == NULL) {
		return -1;
	}

	if(tsvc->client) {
		/* Shutdown the client then free the object */
		torrentClient_shutdown(tsvc->client);
		g_free(tsvc->client);
		tsvc->client = NULL;
	}

	if(tsvc->server) {
		/* Shutdown the server then free the object */
		torrentServer_shutdown(tsvc->server);
		g_free(tsvc->client);
		tsvc->server = NULL;
	}

	if(tsvc->authority) {
		/* Shutdown the client then free the object */
		torrentAuthority_shutdown(tsvc->authority);
		g_free(tsvc->authority);
		tsvc->authority = NULL;
	}

	return 0;
}
