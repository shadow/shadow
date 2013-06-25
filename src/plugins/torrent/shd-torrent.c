/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shd-torrent.h"

Torrent* torrent;

static in_addr_t torrent_resolveHostname(const gchar* hostname) {
	ShadowLogFunc log = torrent->shadowlib->log;
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

static void torrent_report(TorrentClient* tc, gchar* preamble) {
	if(tc != NULL && preamble != NULL) {
		ShadowLogFunc log = torrent->shadowlib->log;
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

		gchar ipStringBuffer[INET_ADDRSTRLEN+1];
		memset(ipStringBuffer, 0, INET_ADDRSTRLEN+1);
		inet_ntop(AF_INET, &(tc->currentBlockTransfer->addr), ipStringBuffer, INET_ADDRSTRLEN);

		log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s first byte from %s in %lu.%.3d seconds, "
				"%d of %d DOWN and %d of %d UP in %lu.%.3d seconds, total %d of %d bytes [%d\%] in %lu.%.3d seconds (block %d of %d  [%d])",
						preamble, ipStringBuffer,
						block_first_time.tv_sec, (gint)(block_first_time.tv_nsec / 1000000),
						tc->currentBlockTransfer->downBytesTransfered, tc->downBlockSize,
						tc->currentBlockTransfer->upBytesTransfered, tc->upBlockSize,
						block_curr_time.tv_sec, (gint)(block_curr_time.tv_nsec / 1000000),
						tc->totalBytesDown, tc->fileSize, (gint)(((gdouble)tc->totalBytesDown / (gdouble)tc->fileSize) * 100),
						curr_time.tv_sec, (gint)(curr_time.tv_nsec / 1000000),
						tc->blocksDownloaded, tc->numBlocks, tc->blocksRemaining);
	}
}

static void torrent_wakeupCallback(gpointer data) {
    torrent_activate();
}

/* called from inner filegetter code when it wants to sleep for some seconds */
static void torrent_sleepCallback(guint seconds) {
    torrent->shadowlib->createCallback(&torrent_wakeupCallback, NULL, seconds*1000);
}

Torrent**  torrent_init(Torrent* currentTorrent) {
	torrent = currentTorrent;
	return &torrent;
}

void torrent_new(int argc, char* argv[]) {
	ShadowLogFunc log = torrent->shadowlib->log;
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "torrent_new called");

	torrent->server = NULL;
	torrent->client = NULL;
	torrent->authority = NULL;
	clock_gettime(CLOCK_REALTIME, &torrent->lastReport);
	torrent->clientDone = 0;

	const gchar* USAGE = "Torrent USAGE: \n"
			"\t'authority port'\n"
			"\t'nodeType (\"client\",\"server\",\"node\") authorityHostname authorityPort socksHostname socksPort serverPort fileSize [downBlockSize upBlockSize]'";
	if(argc < 3) {
		log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
		return;
	}

	gchar *nodeType = argv[1];

	if(!g_ascii_strncasecmp(nodeType, "client", 6) ||
			!g_ascii_strncasecmp(nodeType, "server", 6) || !g_ascii_strncasecmp(nodeType, "node", 4)) {
		if(argc < 5) {
			log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", USAGE);
			return;
		}
		gchar* authHostname = argv[2];
		gint authPort = atoi(argv[3]);
		gchar* socksHostname = argv[4];
		gint socksPort = atoi(argv[5]);
		gint serverPort = atoi(argv[6]);

		gint fileSize = 0;
		if(strstr(argv[7], "KB") != NULL) {
			fileSize = atoi(strtok(argv[7], "K")) * 1024;
		} else if(strstr(argv[7], "MB") != NULL) {
			fileSize = atoi(strtok(argv[7], "M")) * 1024 * 1024;
		} else {
			fileSize = atoi(argv[7]);
		}

		gint downBlockSize = 16*1024;
		gint upBlockSize = 16*1024;

		if(argc == 10) {
			if(strstr(argv[8], "KB") != NULL) {
				downBlockSize = atoi(strtok(argv[8], "K")) * 1024;
			} else if(strstr(argv[8], "MB") != NULL) {
				downBlockSize = atoi(strtok(argv[8], "M")) * 1024 * 1024;
			} else {
				downBlockSize = atoi(argv[8]);
			}

			if(strstr(argv[9], "KB") != NULL) {
				upBlockSize = atoi(strtok(argv[9], "K")) * 1024;
			} else if(strstr(argv[9], "MB") != NULL) {
				upBlockSize = atoi(strtok(argv[9], "M")) * 1024 * 1024;
			} else {
				upBlockSize = atoi(argv[9]);
			}
		}


		if(!g_ascii_strncasecmp(nodeType, "server", 6) || !g_ascii_strncasecmp(nodeType, "node", 4)) {
			/* create an epoll to wait for I/O events */
			gint epolld = epoll_create(1);
			if(epolld == -1) {
				log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
				close(epolld);
				epolld = 0;
			}

			/* start server to listen for connections */
			in_addr_t listenIP = INADDR_ANY;
			in_port_t listenPort = (in_port_t)serverPort;
			in_addr_t authAddr = torrent_resolveHostname(authHostname);

			torrent->server = g_new0(TorrentServer, 1);
			// NOTE: since the up/down block sizes are in context of the client, we swap them for
			// the server since it's actually the reverse of what the client has
			if(torrentServer_start(torrent->server, epolld, htonl(listenIP), htons(listenPort), authAddr, htons(authPort),
					upBlockSize, downBlockSize) < 0) {
				log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "torrent server error, not started!");
				g_free(torrent->server);
				torrent->server = NULL;
				return;
			} else {
				gchar ipStringBuffer[INET_ADDRSTRLEN+1];
				memset(ipStringBuffer, 0, INET_ADDRSTRLEN+1);
				inet_ntop(AF_INET, &listenIP, ipStringBuffer, INET_ADDRSTRLEN);

				log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "torrent server running on at %s:%u", ipStringBuffer, listenPort);
			}
		}

		if(!g_ascii_strncasecmp(nodeType, "client", 6) || !g_ascii_strncasecmp(nodeType, "node", 4)) {
			/* create an epoll to wait for I/O events */
			gint epolld = epoll_create(1);
			if(epolld == -1) {
				log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
				close(epolld);
				epolld = 0;
			}

			/* start up client */
			in_addr_t socksAddr = torrent_resolveHostname(socksHostname);
			in_addr_t authAddr = torrent_resolveHostname(authHostname);

			torrent->client = g_new0(TorrentClient, 1);
			if(torrentClient_start(torrent->client, epolld, socksAddr, htons(socksPort), authAddr, htons(authPort), serverPort,
					fileSize, downBlockSize, upBlockSize) < 0) {
				log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "torrent client error, not started!");
				g_free(torrent->client);
				torrent->client = NULL;
				return;
			} else {
				log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "torrent client running");
			}
		}
	} else if(g_ascii_strncasecmp(nodeType, "authority", 9) == 0) {
		gint authPort = atoi(argv[2]);

		/* create an epoll to wait for I/O events */
		gint epolld = epoll_create(1);
		if(epolld == -1) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
			close(epolld);
			epolld = 0;
		}

		in_addr_t listenIP = INADDR_ANY;
		in_port_t listenPort = (in_port_t)authPort;

		torrent->authority = g_new0(TorrentAuthority, 1);
		if(torrentAuthority_start(torrent->authority, epolld, htonl(listenIP), htons(listenPort), 0) < 0) {
			log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "torrent authority error, not started!");
			g_free(torrent->authority);
			torrent->authority = NULL;
			return;
		} else {
			gchar ipStringBuffer[INET_ADDRSTRLEN+1];
			memset(ipStringBuffer, 0, INET_ADDRSTRLEN+1);
			inet_ntop(AF_INET, &listenIP, ipStringBuffer, INET_ADDRSTRLEN);

			log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "torrent authority running on at %s:%u", ipStringBuffer, listenPort);
		}
	}
}

void torrent_activate() {
	ShadowLogFunc log = torrent->shadowlib->log;
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "torrent_activate called");

	if(torrent->server) {
		if(!torrent->server->epolld) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "server can't wait on epoll without epoll descriptor");
			return;
		}

		struct epoll_event events[10];
		int nfds = epoll_wait(torrent->server->epolld, events, 10, 0);
		if(nfds == -1) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in server epoll_wait");
			return;
		}

		for(int i = 0; i < nfds; i++) {
			gint res = torrentServer_activate(torrent->server, events[i].data.fd, events[i].events);
			if(res < 0) {
				log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "activate returned %d", res);
			}

            if(res == TS_ERR_FATAL) {
                TorrentServer_Connection *conn = g_hash_table_lookup(torrent->server->connections, &(events[i].data.fd));
                if(conn) {
    				gchar ipStringBuffer[INET_ADDRSTRLEN+1];
    				memset(ipStringBuffer, 0, INET_ADDRSTRLEN+1);
    				inet_ntop(AF_INET, &(conn->addr), ipStringBuffer, INET_ADDRSTRLEN);

                    log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Fatal error on server activate with socket %d on address %s",
                                            events[i].data.fd, ipStringBuffer);
                } else {
                    log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Fatal error on server activate with socket %d", events[i].data.fd);
                }

            }
		}

		TorrentServer_PacketInfo *info = (TorrentServer_PacketInfo *)g_queue_pop_head(torrent->server->packetInfo);
		while(info) {
			guint latency = (info->recvTime - info->sendTime) / 1000000;
			log(G_LOG_LEVEL_INFO, __FUNCTION__, "cookie: %4.4X sent: %f recv: %f latency: %d ms",
					info->cookie,  (gdouble)(info->sendTime) / 1000000000.0, (gdouble)(info->recvTime) / 1000000000.0, latency);

			info = (TorrentServer_PacketInfo *)g_queue_pop_head(torrent->server->packetInfo);
		}
	}

	if(torrent->client) {
		struct epoll_event events[10];
		int nfds = epoll_wait(torrent->client->epolld, events, 10, 0);
		if(nfds == -1) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
			return;
		}

		for(int i = 0; i < nfds; i++) {
			gint ret = torrentClient_activate(torrent->client, events[i].data.fd, events[i].events);
			if(ret == TC_ERR_FATAL || ret == TC_ERR_SOCKSCONN) {
                log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "torrent client shutdown with error %d...retrying in 60 seconds", ret);

                torrentClient_shutdown(torrent->client);
                torrentClient_start(torrent->client, torrent->client->epolld, torrent->client->socksAddr, torrent->client->socksPort,
                        torrent->client->authAddr, torrent->client->authPort, torrent->client->serverPort, torrent->client->fileSize,
                        torrent->client->downBlockSize, torrent->client->upBlockSize);

                /* set wakup timer and call sleep function */
                torrent_sleepCallback(60);

                return;
            } else if(ret != TC_SUCCESS && ret != TC_BLOCK_DOWNLOADED && ret != TC_ERR_RECV && ret != TC_ERR_SEND) {
                log(G_LOG_LEVEL_INFO, __FUNCTION__, "torrent client encountered a non-asynch-io related error");
            }

			if(!torrent->clientDone  && torrent->client->totalBytesDown > 0) {
				struct timespec now;
				clock_gettime(CLOCK_REALTIME, &now);

				if(ret == TC_BLOCK_DOWNLOADED) {
					torrent->lastReport = now;
					torrent_report(torrent->client, "[client-block-complete]");
				} else if(now.tv_sec - torrent->lastReport.tv_sec > 1 && torrent->client->currentBlockTransfer != NULL &&
						  (torrent->client->currentBlockTransfer->downBytesTransfered > 0 ||
						   torrent->client->currentBlockTransfer->upBytesTransfered > 0)) {
					torrent->lastReport = now;
					torrent_report(torrent->client, "[client-block-progress]");
				}

				if(torrent->client->blocksDownloaded >= torrent->client->numBlocks) {
					torrent_report(torrent->client, "[client-complete]");
					torrent->clientDone = 1;
					torrent_free();
				}
			}
		}
	} else if(torrent->authority) {
		if(!torrent->authority->epolld) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "authority can't wait on epoll without epoll descriptor");
			return;
		}

		struct epoll_event events[10];
		int nfds = epoll_wait(torrent->authority->epolld, events, 10, 0);
		if(nfds == -1) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in server epoll_wait");
			return;
		}

		for(int i = 0; i < nfds; i++) {
			gint res = torrentAuthority_activate(torrent->authority, events[i].data.fd);
			if(res < 0) {
				log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "activate returned %d", res);
			}
		}
	}
}

void torrent_free() {
	if(torrent->client) {
		/* Shutdown the client then free the object */
		torrentClient_shutdown(torrent->client);
		g_free(torrent->client);
		torrent->client = NULL;
	}

	if(torrent->server) {
		/* Shutdown the server then free the object */
		torrentServer_shutdown(torrent->server);
		g_free(torrent->client);
		torrent->server = NULL;
	}

	if(torrent->authority) {
		/* Shutdown the client then free the object */
		torrentAuthority_shutdown(torrent->authority);
		g_free(torrent->authority);
		torrent->authority = NULL;
	}
}
