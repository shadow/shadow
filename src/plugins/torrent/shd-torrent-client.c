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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <assert.h>

#include <string.h>

#include "shd-torrent.h"

#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

#define TC_ASSERTIO(tc, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return TC_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			tc->errcode = ts_errcode; \
			fprintf(stderr, "torrent client fatal error: %s\n", strerror(errno)); \
			return TC_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		tc->errcode = TC_CLOSED; \
		return TC_ERR_FATAL; \
	}

static void torrentClient_log(TorrentClient *tc, enum torrentClient_loglevel level, const gchar* format, ...) {
	/* if they gave NULL as a callback, dont log */
	if(tc != NULL && tc->log_cb != NULL) {
		va_list vargs, vargs_copy;
		size_t s = sizeof(tc->logBuffer);

		va_start(vargs, format);
		va_copy(vargs_copy, vargs);
		vsnprintf(tc->logBuffer, s, format, vargs);
		va_end(vargs_copy);

		tc->logBuffer[s-1] = '\0';

		(*(tc->log_cb))(level, tc->logBuffer);
	}
}

int torrentClient_changeEpoll(TorrentClient *tc, gint sockd, gint event) {
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = sockd;
	if(epoll_ctl(tc->epolld, EPOLL_CTL_MOD, sockd, &ev) < 0) {
		return TC_ERR_EPOLL;
	}
	return 0;
}

void torrentClient_fillBuffer(gchar *buf, gint length, gchar *filler) {
	memset(buf, 0, length);
	for(int i = 0; i < length - strlen(filler); i += strlen(filler)) {
		memcpy(&buf[i], filler, strlen(filler));
	}
}

static void torrentClient_connectionClose(TorrentClient* tc, TorrentClient_Server* server, int closeSocket) {
	epoll_ctl(tc->epolld, EPOLL_CTL_DEL, server->sockd, NULL);
	g_hash_table_remove(tc->connections, &(server->sockd));
	server->state = TC_SERVER_IDLE;

	tc->servers = g_list_append(tc->servers, server);
	if(closeSocket) {
		close(server->sockd);
		server->sockd = 0;
	}
}

gint torrentClient_start(TorrentClient* tc, gint epolld, in_addr_t socksAddr, in_port_t socksPort, in_addr_t authAddr, in_port_t authPort,
		in_port_t serverPort, gint fileSize, gint downBlockSize, gint upBlockSize) {
	tc->serverPort = serverPort;
	tc->maxConnections = -1;
	tc->epolld = epolld;
	tc->servers = NULL;
	tc->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);

	tc->socksAddr = socksAddr;
	tc->socksPort = socksPort;
	tc->authAddr = authAddr;
	tc->authPort = authPort;

	tc->totalBytesDown = 0;
	tc->totalBytesUp = 0;
	tc->fileSize = fileSize;
	tc->downBlockSize = downBlockSize;
	tc->upBlockSize = upBlockSize;
	tc->bytesInProgress = 0;
	// TODO: This is for some reason not accurate
	// ceil(fileSize / downBlockSize)
	tc->numBlocks = (fileSize + downBlockSize - 1) / downBlockSize;
	tc->blocksRemaining = tc->numBlocks;
	tc->blocksDownloaded = 0;
	tc->currentBlockTransfer = NULL;

	gint sockd = torrentClient_connect(tc, authAddr, authPort);
	if(sockd < 0) {
		return sockd;
	}
	tc->authSockd = sockd;

	TorrentClient_Server* server = g_new0(TorrentClient_Server, 1);
	server->addr = authAddr;
	server->port = authPort;
	server->sockd = sockd;
	server->state = TC_AUTH_REQUEST_NODES;
	g_hash_table_insert(tc->connections, &server->sockd, server);

	torrentClient_changeEpoll(tc, sockd, EPOLLOUT);

	return 0;
}

gint torrentClient_activate(TorrentClient *tc, gint sockd, gint events) {
	gchar buf[TC_BUF_SIZE];
	ssize_t bytes;
	enum torrentClient_code ret = TC_SUCCESS;

	TorrentClient_Server* server = g_hash_table_lookup(tc->connections, &sockd);
	if(server == NULL) {
		return TC_ERR_NOSERVER;
	}

	start:
	switch(server->state) {
		case TC_SOCKS_REQUEST_INIT: {
			/* check that we actually have FT_SOCKS_INIT_LEN space */
			assert(sizeof(server->buf) - server->buf_write_offset >= TC_SOCKS_INIT_LEN);

			/* write the request to our buffer */
			memcpy(server->buf + server->buf_write_offset, TC_SOCKS_INIT, TC_SOCKS_INIT_LEN);

			server->buf_write_offset += TC_SOCKS_INIT_LEN;

			/* we are ready to send, then transition to socks init reply */
			server->state = TC_SEND;
			server->nextstate = TC_SOCKS_TOREPLY_INIT;

			torrentClient_changeEpoll(tc, sockd, EPOLLOUT);

			goto start;
		}

		case TC_SOCKS_TOREPLY_INIT: {
			torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			server->state = TC_RECEIVE;
			server->nextstate = TC_SOCKS_REPLY_INIT;
			goto start;
		}

		case TC_SOCKS_REPLY_INIT: {
			/* if we didnt get it all, go back for more */
			if(server->buf_write_offset - server->buf_read_offset < 2) {
				server->state = TC_SOCKS_TOREPLY_INIT;
				goto start;
			}

			/* must be version 5 */
			if(server->buf[server->buf_read_offset] != 0x05) {
				return TC_ERR_SOCKSINIT;
			}
			/* must be success */
			if(server->buf[server->buf_read_offset + 1] != 0x00) {
				return TC_ERR_SOCKSINIT;
			}

			server->buf_read_offset += 2;

			/* now send the socks connection request */
			server->state = TC_SOCKS_REQUEST_CONN;

			goto start;
		}

		case TC_SOCKS_REQUEST_CONN: {
			/* check that we actually have TC_SOCKS_REQ_HEAD_LEN+6 space */
			assert(sizeof(server->buf) - server->buf_write_offset >= TC_SOCKS_REQ_HEAD_LEN + 6);

			in_addr_t addr = server->addr;
			in_port_t port = htons(server->port);

			/* write connection request, including intended destination */
			memcpy(server->buf + server->buf_write_offset, TC_SOCKS_REQ_HEAD, TC_SOCKS_REQ_HEAD_LEN);
			server->buf_write_offset += TC_SOCKS_REQ_HEAD_LEN;
			memcpy(server->buf + server->buf_write_offset, &(addr), 4);
			server->buf_write_offset += 4;
			memcpy(server->buf + server->buf_write_offset, &(port), 2);
			server->buf_write_offset += 2;

			/* we are ready to send, then transition to socks conn reply */
			server->state = TC_SEND;
			server->nextstate = TC_SOCKS_TOREPLY_CONN;
			torrentClient_changeEpoll(tc, sockd, EPOLLOUT);

			goto start;
		}

		case TC_SOCKS_TOREPLY_CONN: {
			torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			server->state = TC_RECEIVE;
			server->nextstate = TC_SOCKS_REPLY_CONN;
			goto start;
		}

		case TC_SOCKS_REPLY_CONN: {
			/* if we didnt get it all, go back for more */
			if(server->buf_write_offset - server->buf_read_offset < 10) {
				server->state = TC_SOCKS_TOREPLY_CONN;
				goto start;
			}

			/* must be version 5 */
			if(server->buf[server->buf_read_offset] != 0x05) {
				return TC_ERR_SOCKSCONN;
			}

			/* must be success */
			if(server->buf[server->buf_read_offset + 1] != 0x00) {
				return TC_ERR_SOCKSCONN;
			}

			/* check address type for IPv4 */
			if(server->buf[server->buf_read_offset + 3] != 0x01) {
				return TC_ERR_SOCKSCONN;
			}

			/* get address server told us */
			in_addr_t socks_bind_addr;
			in_port_t socks_bind_port;
			memcpy(&socks_bind_addr, &(server->buf[server->buf_read_offset + 4]), 4);
			memcpy(&socks_bind_port, &(server->buf[server->buf_read_offset + 8]), 2);

			server->buf_read_offset += 10;

			/* if we were send a new address, we need to reconnect there */
			if(socks_bind_addr != 0 && socks_bind_port != 0) {
				/* reconnect at new address */
				close(tc->sockd);
				if(torrentClient_connect(tc, socks_bind_addr, socks_bind_port) != TC_SUCCESS) {
					return TC_ERR_SOCKSCONN;
				}
			}

			/* now we are ready to send the http request */
			server->state = TC_SERVER_REQUEST;
			server->nextstate = TC_SERVER_REQUEST;

			torrentClient_changeEpoll(tc, sockd, EPOLLOUT);

			goto start;
		}

		case TC_SEND: {
			assert(server->buf_write_offset >= server->buf_read_offset);

			gpointer sendpos = server->buf + server->buf_read_offset;
			size_t sendlen = server->buf_write_offset - server->buf_read_offset;

			ssize_t bytes = send(sockd, sendpos, sendlen, 0);

			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TC_ERR_SEND);

			server->buf_read_offset += bytes;

			if(server->buf_read_offset == server->buf_write_offset) {
				/* we've sent everything we can, reset offsets */
				server->buf_read_offset = 0;
				server->buf_write_offset = 0;

				/* now we go to the next state */
				server->state = server->nextstate;
			}

			/* either next state or try to send more */
			goto start;
		}

		case TC_RECEIVE: {
			size_t space = sizeof(server->buf) - server->buf_write_offset;

			/* we will recv from socket and write to buf */
			gpointer recvpos = server->buf + server->buf_write_offset;
			ssize_t bytes = recv(sockd, recvpos, space, 0);

			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK, TC_ERR_RECV);

			server->buf_write_offset += bytes;

			/* go to the next state to check new data */
			server->state = server->nextstate;

			goto start;
		}

//		case TC_AUTH_REGISTER:
//			buf[0] = 0x01;
//			memcpy(&buf[1], &(tc->serverPort), sizeof(tc->serverPort));
//
//			bytes = send(sockd, buf, sizeof(tc->serverPort) + 1, 0);
//			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TC_ERR_SEND);
//
//			torrentClient_changeEpoll(tc, sockd, EPOLLIN);
//			server->state = TC_AUTH_REQUEST_NODES;
//			//break;


		case TC_AUTH_REQUEST_NODES:
			buf[0] = TA_MSG_REQUEST_NODES;
			bytes = send(sockd, buf, 1, 0);
			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TC_ERR_SEND);
			torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			server->state = TC_AUTH_RECEIVE_NODES;
			break;

		case TC_AUTH_RECEIVE_NODES:
			bytes = recv(sockd, buf, 1024, 0);
			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK, TC_ERR_RECV);
			gint numNodes = buf[0];
			gint offset = 1;
			for(int i = 0; i < numNodes; i++) {
				in_addr_t addr;
				in_port_t port;

				memcpy(&addr, buf + offset, sizeof(addr));
				offset += sizeof(addr);
				memcpy(&port, buf + offset, sizeof(port));
				offset += sizeof(port);

				int found = 0;
				GList *currItem = tc->servers;
				while(!found && currItem != NULL) {
					TorrentClient_Server *server = currItem->data;
					if(server->addr == addr && server->port == port) {
						found = 1;
					}
					currItem = g_list_next(currItem);
				}

				if(!found) {
					TorrentClient_Server* server = g_new0(TorrentClient_Server, 1);
					server->addr = addr;
					server->port = port;
					if(tc->socksAddr == htonl(INADDR_NONE)) {
						server->sockd = torrentClient_connect(tc, server->addr, htons(server->port));
						server->state = TC_SERVER_REQUEST;
					} else {
						server->sockd = torrentClient_connect(tc, tc->socksAddr, tc->socksPort);
						server->state = TC_SOCKS_REQUEST_INIT;
					}
					server->cookie = rand() % G_MAXUINT;

					if(server->sockd < 0) {
						return TC_ERR_CONNECT;
					}

					server->downBytesTransfered = 0;
					server->upBytesTransfered = 0;
					server->buf_read_offset = 0;
					server->buf_write_offset = 0;
					torrentClient_changeEpoll(tc, server->sockd, EPOLLOUT);
					tc->servers = g_list_append(tc->servers, server);
					g_hash_table_insert(tc->connections, &(server->sockd), server);
				}
			}
			clock_gettime(CLOCK_REALTIME, &tc->lastServerListFetch);

			torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			server->state = TC_AUTH_RECEIVE_NODES;

			break;

		case TC_SERVER_REQUEST:
			if(tc->totalBytesDown == 0) {
				clock_gettime(CLOCK_REALTIME, &(tc->download_start));
			}

			if(tc->blocksRemaining <= 0) {
				server->state = TC_SERVER_IDLE;
				torrentClient_changeEpoll(tc, sockd, EPOLLIN);
				break;
			}

			gchar request[64];
			sprintf(request, "FILE REQUEST\r\nTOR-COOKIE: %8.8X\r\n", server->cookie);

			bytes = send(sockd, request, strlen(request), 0);
			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TC_ERR_SEND);
			torrentClient_changeEpoll(tc, sockd, EPOLLIN);

			tc->blocksRemaining--;

			server->downBytesTransfered = 0;
			server->upBytesTransfered = 0;
			server->buf_read_offset = 0;
			server->buf_write_offset = 0;
			server->state = TC_SERVER_TRANSFER;
			clock_gettime(CLOCK_REALTIME, &(server->download_start));
			break;

		case TC_SERVER_TRANSFER: {
			if(events & EPOLLIN && server->downBytesTransfered < tc->downBlockSize) {
				int remainingBytes = tc->downBlockSize - server->downBytesTransfered;
				int len = (remainingBytes < sizeof(buf) ? remainingBytes : sizeof(buf));
				bytes = recv(sockd, buf, len, 0);
				TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK, TC_ERR_RECV);

				if(tc->totalBytesDown == 0) {
					clock_gettime(CLOCK_REALTIME, &(tc->download_first_byte));
				}

				if(server->downBytesTransfered == 0) {
					clock_gettime(CLOCK_REALTIME, &(server->download_first_byte));
				}

				server->downBytesTransfered += bytes;
				tc->totalBytesDown += bytes;
				tc->bytesInProgress -= bytes;
			}

			if(events & EPOLLOUT && server->upBytesTransfered < tc->upBlockSize) {
				int remainingBytes = tc->upBlockSize - server->upBytesTransfered;
				int len = (remainingBytes < sizeof(buf) ? remainingBytes : sizeof(buf));

				struct timespec now;
				clock_gettime(CLOCK_REALTIME, &now);
				gchar filler[64];
				sprintf(filler, TC_BUF_FILLER, server->cookie, TIME_TO_NS(now));
				torrentClient_fillBuffer(buf, len, filler);

				bytes = send(sockd, buf, len, 0);
				TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TC_ERR_SEND);

				server->upBytesTransfered += bytes;
				tc->totalBytesUp += bytes;
			}

			tc->currentBlockTransfer = server;

			if(server->downBytesTransfered >= tc->downBlockSize && server->upBytesTransfered >= tc->upBlockSize) {
				server->state = TC_SERVER_FINISHED;
				torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			} else if(server->downBytesTransfered >= tc->downBlockSize) {
				torrentClient_changeEpoll(tc, sockd, EPOLLOUT);
			} else if(server->upBytesTransfered >= tc->upBlockSize) {
				torrentClient_changeEpoll(tc, sockd, EPOLLIN);
			}

			break;
		}

		case TC_SERVER_FINISHED: {
			bytes = recv(sockd, buf, sizeof(buf), 0);
			TC_ASSERTIO(tc, bytes, errno == EWOULDBLOCK, TC_ERR_RECV);

			gchar *found = strcasestr(buf, "FINISHED");
			if(found) {
				server->state = TC_SERVER_REQUEST;
				clock_gettime(CLOCK_REALTIME, &(server->download_end));

				tc->currentBlockTransfer = server;
				tc->blocksDownloaded++;
				ret = TC_BLOCK_DOWNLOADED;
				torrentClient_changeEpoll(tc, sockd, EPOLLOUT);
			}
			break;
		}

		default:
			break;
	}

	return ret;
}

gint torrentClient_shutdown(TorrentClient* tc) {
	for(int i = 0; i < g_list_length(tc->servers); i++) {
		TorrentClient_Server *server = g_list_nth(tc->servers, i)->data;
		if(server->sockd != 0) {
			torrentClient_connectionClose(tc, server, 1);
		}
	}

	g_hash_table_destroy(tc->connections);

	return TC_SUCCESS;
}

gint torrentClient_connect(TorrentClient *tc, in_addr_t addr, in_port_t port) {
	/* create socket */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(sockd < 0) {
		return TC_ERR_SOCKET;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = addr;
	server.sin_port = port;

	gint result = connect(sockd,(struct sockaddr *) &server, sizeof(server));
	/* nonblocking sockets means inprogress is ok */
	if(result < 0 && errno != EINPROGRESS) {
		return TC_ERR_CONNECT;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = sockd;
	if(epoll_ctl(tc->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return TC_ERR_EPOLL;
	}

	return sockd;
}



