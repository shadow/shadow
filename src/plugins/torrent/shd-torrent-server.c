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

#include <string.h>

#include "shd-torrent.h"

#define TIME_TO_NS(ts) ((ts.tv_sec * 1000000000) + ts.tv_nsec)

#define TS_ASSERTIO(ts, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return TS_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			ts->errcode = ts_errcode; \
			fprintf(stderr, "torrent server fatal error: %s\n", strerror(errno)); \
			return TC_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		ts->errcode = TS_CLOSED; \
		return TS_ERR_FATAL; \
	}

void torrentServer_changeEpoll(TorrentServer *ts, gint sockd, gint event) {
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = sockd;
	epoll_ctl(ts->epolld, EPOLL_CTL_MOD, sockd, &ev);
}

void torrentServer_populateCookies(gchar *buf, gint length, guint32 cookie) {
	gchar buffer[32];
	sprintf(buffer, "TOR-COOKIE: %8.8X\r\n", cookie);
	memset(buf, 0, length);
	for(int i = 0; i < length - 18; i += 18) {
		//memcpy(&buf[i], buffer, 18);
		sprintf(&buf[i], "TOR-COOKIE: %8.8X\r\n", cookie);
	}
}


gint torrentServer_connect(TorrentServer *ts, in_addr_t addr, in_port_t port) {
	/* create socket */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(sockd < 0) {
		return TS_ERR_SOCKET;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = addr;
	server.sin_port = port;

	gint result = connect(sockd,(struct sockaddr *) &server, sizeof(server));
	/* nonblocking sockets means inprogress is ok */
	if(result < 0 && errno != EINPROGRESS) {
		return TS_ERR_CONNECT;
	}

	/* start watshing socket */
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = sockd;
	if(epoll_ctl(ts->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return TS_ERR_EPOLL;
	}

	return sockd;
}

void torrentServer_connectionClose(TorrentServer *ts, TorrentServer_Connection *connection) {
	epoll_ctl(ts->epolld, EPOLL_CTL_DEL, connection->sockd, NULL);
	g_hash_table_remove(ts->connections, &(connection->sockd));
}

static void torrentServer_connection_destroy_cb(gpointer data) {
	TorrentServer_Connection *conn = data;

	if(conn != NULL) {
		close(conn->sockd);
		free(conn);
	}
}

gint torrentServer_start(TorrentServer* ts, gint epolld, in_addr_t listenAddr, in_port_t listenPort,
		in_addr_t authAddr, in_port_t authPort, gint downBlockSize, gint upBlockSize) {
	if(ts == NULL) {
		return TS_ERR_FATAL;
	}

	/* create the socket and get a socket descriptor */
	gint sockd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if (sockd == -1) {
		return TS_ERR_SOCKET;
	}

	/* setup the socket address info, client has outgoing connection to server */
	struct sockaddr_in listener;
	memset(&listener, 0, sizeof(listener));
	listener.sin_family = AF_INET;
	listener.sin_addr.s_addr = listenAddr;
	listener.sin_port = listenPort;

	/* bind the socket to the server port */
	gint result = bind(sockd, (struct sockaddr *) &listener, sizeof(listener));
	if (result == -1) {
		return TS_ERR_BIND;
	}

	/* set as server socket that will listen for clients */
	result = listen(sockd, 10);
	if (result == -1) {
		return TS_ERR_LISTEN;
	}

	/* create our server and store our server socket */
	memset(ts, 0, sizeof(TorrentServer));
	ts->listenSockd = sockd;
	ts->epolld = epolld;
	ts->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, torrentServer_connection_destroy_cb);
	ts->packetInfo = g_queue_new();

	ts->downBlockSize = downBlockSize;
	ts->upBlockSize = upBlockSize;

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = ts->listenSockd;
	if(epoll_ctl(ts->epolld, EPOLL_CTL_ADD, ts->listenSockd, &ev) < 0) {
		return TS_ERR_EPOLL;
	}

	sockd = torrentServer_connect(ts, authAddr, authPort);
	if(sockd < 0) {
		return sockd;
	}
	ts->authSockd = sockd;
	ts->serverPort = ntohs(listenPort);

	TorrentServer_Connection *connection = g_new0(TorrentServer_Connection, 1);
	connection->sockd = sockd;
	connection->addr = authAddr;
	connection->state = TS_AUTH_REGISTER;
	connection->downBytesTransfered = 0;
	connection->upBytesTransfered = 0;
	g_hash_table_insert(ts->connections, &(connection->sockd), connection);

	torrentServer_changeEpoll(ts, sockd, EPOLLOUT);

	return TS_SUCCESS;
}

gint torrentServer_activate(TorrentServer *ts, gint sockd, gint events) {
	if(ts == NULL || sockd < 0) {
		return TS_ERR_FATAL;
	}

	if(sockd == ts->listenSockd) {
		gint res = 0;
		do {
			res = torrentServer_accept(ts, NULL);
		} while(!res);
		return res;
	}

	/* otherwise check for a connections */
	TorrentServer_Connection *connection = g_hash_table_lookup(ts->connections, &sockd);
	if(connection == NULL) {
		return TS_ERR_NOCONN;
	}

	gchar buf[TS_BUF_SIZE];
	ssize_t bytes = -1;

	switch(connection->state) {
		case TS_AUTH_IDLE:
			break;
		case TS_AUTH_REGISTER:
			buf[0] = TA_MSG_REGISTER;
			memcpy(&buf[1], &(ts->serverPort), sizeof(ts->serverPort));

			bytes = send(sockd, buf, sizeof(ts->serverPort) + 1, 0);
			TS_ASSERTIO(ts, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TS_ERR_SEND);

			if(bytes > 0) {
				torrentServer_changeEpoll(ts, sockd, EPOLLIN);
				connection->state = TS_AUTH_IDLE;
			}

			break;
		case TS_IDLE:
			connection->downBytesTransfered = 0;
			connection->upBytesTransfered = 0;
			torrentServer_changeEpoll(ts, sockd, EPOLLIN);
			connection->state = TS_REQUEST;

		case TS_REQUEST:
			bytes = recv(sockd, buf, sizeof(buf), 0);
			TS_ASSERTIO(ts, bytes, errno == EWOULDBLOCK, TS_ERR_RECV);

			gchar *found = strcasestr(buf, "FILE REQUEST");
			if(!found) {
				connection->state = TS_REQUEST;
			} else {
				gchar *torCookie = strcasestr(buf, "TOR-COOKIE: ");
				if(torCookie) {
					connection->cookie = g_ascii_strtoll(torCookie += strlen("TOR-COOKIE: "), NULL, 16);
				}
				connection->state = TS_TRANSFER;
				torrentServer_changeEpoll(ts, sockd, EPOLLIN | EPOLLOUT);
			}
			break;

		case TS_TRANSFER: {
			if(events & EPOLLIN && connection->downBytesTransfered < ts->downBlockSize) {
				int remainingBytes = ts->downBlockSize - connection->downBytesTransfered;
				int len = (remainingBytes < sizeof(buf) ? remainingBytes : sizeof(buf));
				bytes = recv(sockd, buf, len, 0);
				TS_ASSERTIO(ts, bytes, errno == EWOULDBLOCK, TS_ERR_RECV);
				connection->downBytesTransfered += bytes;

				/* look for time stamp in buffer to compute overall latency */
				gchar *timestamp = strcasestr(buf, "TIME: ");
				if(timestamp) {
					/* extract the sent time from the packet */
					guint64 sendTime = g_ascii_strtoll(timestamp + strlen("TIME: "), NULL, 10);
					struct timespec now;
					clock_gettime(CLOCK_REALTIME, &now);

					TorrentServer_PacketInfo *packetInfo = g_new0(TorrentServer_PacketInfo, 1);
					packetInfo->sendTime = sendTime;
					packetInfo->recvTime = TIME_TO_NS(now);
					gchar *cookie = strcasestr(buf, "TOR-COOKIE: ");
					if(cookie) {
						packetInfo->cookie = g_ascii_strtoll(cookie + strlen("TOR-COOKIE: "), NULL, 16);
					}

					/* push both the send and recv times on the queue */
					g_queue_push_tail(ts->packetInfo, (gpointer)packetInfo);
				}
			}

			if(events & EPOLLOUT && connection->upBytesTransfered < ts->upBlockSize) {
				int remainingBytes = ts->upBlockSize - connection->upBytesTransfered;
				int len = (remainingBytes < sizeof(buf) ? remainingBytes : sizeof(buf));
				torrentServer_populateCookies(buf, len, connection->cookie);

				bytes = send(sockd, buf, len, 0);
				TS_ASSERTIO(ts, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TS_ERR_SEND);

				connection->upBytesTransfered += bytes;
			}


			if(connection->downBytesTransfered >= ts->downBlockSize && connection->upBytesTransfered >= ts->upBlockSize) {
				connection->state  = TS_FINISHED;
				torrentServer_changeEpoll(ts, sockd, EPOLLOUT);
			} else if(connection->downBytesTransfered >= ts->downBlockSize) {
				torrentServer_changeEpoll(ts, sockd, EPOLLOUT);
			} else if(connection->upBytesTransfered >= ts->upBlockSize) {
				torrentServer_changeEpoll(ts, sockd, EPOLLIN);
			}

			break;
		}

		case TS_FINISHED: {
			sprintf(buf, "FINISHED");
			bytes = send(sockd, buf, strlen(buf), 0);
			TS_ASSERTIO(ts, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TS_ERR_SEND);
			connection->state  = TS_IDLE;
			torrentServer_changeEpoll(ts, sockd, EPOLLIN);
			break;
		}
	}

	return TS_SUCCESS;
}

gint torrentServer_accept(TorrentServer* ts, gint* sockdOut) {
	if(ts == NULL) {
		return TS_ERR_FATAL;
	}

	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	/* try to accept a connection */
	gint sockd = accept(ts->listenSockd, (struct sockaddr *)(&addr), &addrlen);
	if(sockd < 0) {
		return TS_ERR_ACCEPT;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockd;
	if(epoll_ctl(ts->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return TS_ERR_EPOLL;
	}

	TorrentServer_Connection *connection = g_new0(TorrentServer_Connection, 1);
	connection->sockd = sockd;
	connection->addr = addr.sin_addr.s_addr;
	connection->state = TS_IDLE;
	connection->downBytesTransfered = 0;
	connection->upBytesTransfered = 0;

	g_hash_table_replace(ts->connections, &(connection->sockd), connection);
	if(sockdOut != NULL) {
		*sockdOut = sockd;
	}

	return 0;
}

gint torrentServer_shutdown(TorrentServer* ts) {
	/* destroy the hashtable. this calls the connection destroy function for each. */
	g_hash_table_destroy(ts->connections);

	epoll_ctl(ts->epolld, EPOLL_CTL_DEL, ts->listenSockd, NULL);
	if(close(ts->listenSockd) < 0) {
		return TS_ERR_CLOSE;
	}

	return TS_SUCCESS;
}
