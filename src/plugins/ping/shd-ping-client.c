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

#include "shd-ping-client.h"

#define PING_CLIENT_ASSERTIO(ping, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return PING_CLIENT_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			fprintf(stderr, "ping fatal error: %s\n", strerror(errno)); \
			return PING_CLIENT_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		return PING_CLIENT_ERR_FATAL; \
	}

void pingClient_changeEpoll(gint epolld, gint sockd, gint event) {
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = sockd;
	epoll_ctl(epolld, EPOLL_CTL_MOD, sockd, &ev);
}

gint pingClient_connect(PingClient *pingClient, in_addr_t addr, in_port_t port) {
	/* create socket */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(sockd < 0) {
		return PING_CLIENT_ERR_SOCKET;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = addr;
	server.sin_port = port;

	gint result = connect(sockd,(struct sockaddr *) &server, sizeof(server));
	/* nonblocking sockets means inprogress is ok */
	if(result < 0 && errno != EINPROGRESS) {
		return PING_CLIENT_ERR_CONNECT;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = sockd;
	if(epoll_ctl(pingClient->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return PING_CLIENT_ERR_EPOLL;
	}

	return sockd;
}


void pingClient_sendPing(PingClient *pingClient) {
	struct timespec now;
	clock_gettime(CLOCK_REALTIME, &now);

	GString *buf = g_string_new("");
	while(buf->len < pingClient->pingSize) {
		g_string_append_printf(buf, "TOR-COOKIE: %8.8X\r\nTIME: %lu\r\n\r\n", pingClient->cookie, TIME_TO_NS(now));
	}

	gint sockd = pingClient->sockd;
	for(gint i = 0; i < 1; i++) {
		gint bytes = send(sockd, buf->str,  pingClient->pingSize, 0);
		//PING_CLIENT_ASSERTIO(ping, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, PING_CLIENT_ERR_SEND);
		if(bytes < 0 && errno != EWOULDBLOCK && errno != ENOTCONN && errno != EALREADY) {
            pingClient_shutdown(pingClient);
            pingClient_start(pingClient, pingClient->epolld, pingClient->socksAddr, pingClient->socksPort,
                    pingClient->serverAddr, pingClient->serverPort, pingClient->pingInterval, pingClient->pingSize);

            /* set wakeup timer and call sleep function */
            pingClient->createCallback((ShadowPluginCallbackFunc)pingClient_wakeup, pingClient, 60);
		} else {
			gint64 nanoseconds = TIME_TO_NS(now);
			g_queue_push_tail(pingClient->pingTimes, (gpointer)nanoseconds);
			pingClient->pingsSent++;
			pingClient->createCallback(pingClient_sendPing, pingClient, (guint)pingClient->pingInterval);
		}
	}
	g_string_free(buf, TRUE);

}

void pingClient_wakeup(PingClient *pingClient) {
	pingClient_activate(pingClient, pingClient->sockd);
}

gint pingClient_start(PingClient *pingClient, gint epolld, in_addr_t socksAddr, in_port_t socksPort, in_addr_t serverAddr, in_port_t serverPort,
		gint pingInterval, gint pingSize) {
	in_addr_t addr;
	in_addr_t port;
	if(socksAddr == htonl(INADDR_NONE)) {
		addr = serverAddr;
		port = serverPort;
		pingClient->clientState = PING_CLIENT_PING;
	} else {
		addr = socksAddr;
		port = socksPort;
		pingClient->clientState = PING_CLIENT_SOCKS_REQUEST_INIT;
	}

	pingClient->epolld = epolld;
	gint sockd = pingClient_connect(pingClient, addr, port);
	if(sockd < 0) {
		return sockd;
	}
	pingClient->sockd = sockd;
	pingClient_changeEpoll(epolld, sockd, EPOLLOUT);

	pingClient->pingInterval = pingInterval;
	pingClient->pingSize = pingSize;
	pingClient->pingTimes = g_queue_new();
	pingClient->pingsSent = 0;
	pingClient->cookie = rand() % G_MAXUINT32;

	pingClient->socksAddr = socksAddr;
	pingClient->socksPort = socksPort;
	pingClient->serverAddr = serverAddr;
	pingClient->serverPort = serverPort;

	return PING_CLIENT_SUCCESS;
}


gint pingClient_activate(PingClient *pingClient, gint sockd) {
	start:
	switch(pingClient->clientState) {
		case PING_CLIENT_SOCKS_REQUEST_INIT: {
			/* check that we actually have FT_SOCKS_INIT_LEN space */
			assert(sizeof(pingClient->buf) - pingClient->buf_write_offset >= PING_CLIENT_SOCKS_INIT_LEN);

			/* write the request to our buffer */
			memcpy(pingClient->buf + pingClient->buf_write_offset, PING_CLIENT_SOCKS_INIT, PING_CLIENT_SOCKS_INIT_LEN);

			pingClient->buf_write_offset += PING_CLIENT_SOCKS_INIT_LEN;

			/* we are ready to send, then transition to socks init reply */
			pingClient->clientState = PING_CLIENT_SEND;
			pingClient->clientNextstate = PING_CLIENT_SOCKS_TOREPLY_INIT;

			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLOUT);

			goto start;
		}

		case PING_CLIENT_SOCKS_TOREPLY_INIT: {
			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLIN);
			pingClient->clientState = PING_CLIENT_RECEIVE;
			pingClient->clientNextstate = PING_CLIENT_SOCKS_REPLY_INIT;
			goto start;
		}

		case PING_CLIENT_SOCKS_REPLY_INIT: {
			/* if we didnt get it all, go back for more */
			if(pingClient->buf_write_offset - pingClient->buf_read_offset < 2) {
				pingClient->clientState = PING_CLIENT_SOCKS_TOREPLY_INIT;
				goto start;
			}

			/* must be version 5 */
			if(pingClient->buf[pingClient->buf_read_offset] != 0x05) {
				return PING_CLIENT_ERR_SOCKSINIT;
			}
			/* must be success */
			if(pingClient->buf[pingClient->buf_read_offset + 1] != 0x00) {
				return PING_CLIENT_ERR_SOCKSINIT;
			}

			pingClient->buf_read_offset += 2;

			/* now send the socks connection request */
			pingClient->clientState = PING_CLIENT_SOCKS_REQUEST_CONN;

			goto start;
		}

		case PING_CLIENT_SOCKS_REQUEST_CONN: {
			/* check that we actually have PING_CLIENT_SOCKS_REQ_HEAD_LEN+6 space */
			assert(sizeof(pingClient->buf) - pingClient->buf_write_offset >= PING_CLIENT_SOCKS_REQ_HEAD_LEN + 6);

			in_addr_t addr = pingClient->serverAddr;
			in_port_t port = pingClient->serverPort;

			/* write connection request, including intended destination */
			memcpy(pingClient->buf + pingClient->buf_write_offset, PING_CLIENT_SOCKS_REQ_HEAD, PING_CLIENT_SOCKS_REQ_HEAD_LEN);
			pingClient->buf_write_offset += PING_CLIENT_SOCKS_REQ_HEAD_LEN;
			memcpy(pingClient->buf + pingClient->buf_write_offset, &(addr), 4);
			pingClient->buf_write_offset += 4;
			memcpy(pingClient->buf + pingClient->buf_write_offset, &(port), 2);
			pingClient->buf_write_offset += 2;

			/* we are ready to send, then transition to socks conn reply */
			pingClient->clientState = PING_CLIENT_SEND;
			pingClient->clientNextstate = PING_CLIENT_SOCKS_TOREPLY_CONN;
			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLOUT);

			goto start;
		}

		case PING_CLIENT_SOCKS_TOREPLY_CONN: {
			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLIN);
			pingClient->clientState = PING_CLIENT_RECEIVE;
			pingClient->clientNextstate = PING_CLIENT_SOCKS_REPLY_CONN;
			goto start;
		}

		case PING_CLIENT_SOCKS_REPLY_CONN: {
			/* if we didnt get it all, go back for more */
			if(pingClient->buf_write_offset - pingClient->buf_read_offset < 10) {
				pingClient->clientState = PING_CLIENT_SOCKS_TOREPLY_CONN;
				goto start;
			}

			/* must be version 5 */
			if(pingClient->buf[pingClient->buf_read_offset] != 0x05) {
				return PING_CLIENT_ERR_SOCKSCONN;
			}

			/* must be success */
			if(pingClient->buf[pingClient->buf_read_offset + 1] != 0x00) {
				return PING_CLIENT_ERR_SOCKSCONN;
			}

			/* check address type for IPv4 */
			if(pingClient->buf[pingClient->buf_read_offset + 3] != 0x01) {
				return PING_CLIENT_ERR_SOCKSCONN;
			}

			/* get address server told us */
			in_addr_t socks_bind_addr;
			in_port_t socks_bind_port;
			memcpy(&socks_bind_addr, &(pingClient->buf[pingClient->buf_read_offset + 4]), 4);
			memcpy(&socks_bind_port, &(pingClient->buf[pingClient->buf_read_offset + 8]), 2);

			pingClient->buf_read_offset += 10;

			/* if we were send a new address, we need to reconnect there */
//			if(socks_bind_addr != 0 && socks_bind_port != 0) {
//				/* reconnect at new address */
//				close(sockd);
//				if(torrentClient_connect(tc, socks_bind_addr, socks_bind_port) != PING_CLIENT_SUCCESS) {
//					return PING_CLIENT_ERR_SOCKSCONN;
//				}
//			}

			/* now we are ready to send the http request */
			pingClient->clientState = PING_CLIENT_PING;
			pingClient->clientNextstate = PING_CLIENT_PING;

			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLOUT);

			goto start;
		}

		case PING_CLIENT_SEND: {
			assert(pingClient->buf_write_offset >= pingClient->buf_read_offset);

			gpointer sendpos = pingClient->buf + pingClient->buf_read_offset;
			size_t sendlen = pingClient->buf_write_offset - pingClient->buf_read_offset;

			ssize_t bytes = send(sockd, sendpos, sendlen, 0);

			PING_CLIENT_ASSERTIO(ping, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, PING_CLIENT_ERR_SEND);

			pingClient->buf_read_offset += bytes;

			if(pingClient->buf_read_offset == pingClient->buf_write_offset) {
				/* we've sent everything we can, reset offsets */
				pingClient->buf_read_offset = 0;
				pingClient->buf_write_offset = 0;

				/* now we go to the next state */
				pingClient->clientState = pingClient->clientNextstate;
			}

			/* either next state or try to send more */
			goto start;
		}

		case PING_CLIENT_RECEIVE: {
			size_t space = sizeof(pingClient->buf) - pingClient->buf_write_offset;

			/* we will recv from socket and write to buf */
			gpointer recvpos = pingClient->buf + pingClient->buf_write_offset;
			ssize_t bytes = recv(sockd, recvpos, space, 0);

			PING_CLIENT_ASSERTIO(ping, bytes, errno == EWOULDBLOCK, PING_CLIENT_ERR_RECV);

			pingClient->buf_write_offset += bytes;

			/* go to the next state to check new data */
			pingClient->clientState = pingClient->clientNextstate;

			goto start;
		}

		case PING_CLIENT_PING: {
			pingClient_changeEpoll(pingClient->epolld, sockd, EPOLLIN);
			pingClient->clientState = PING_CLIENT_IDLE;

			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			guint timeToPing = (guint)((1000000000 - now.tv_nsec) / 1000000);
			pingClient->createCallback(pingClient_sendPing, pingClient, timeToPing);

			break;
		}

		case PING_CLIENT_IDLE: {
			break;
		}
	}

	return PING_CLIENT_SUCCESS;
}

gint pingClient_shutdown(PingClient *pingClient) {
	epoll_ctl(pingClient->epolld, EPOLL_CTL_DEL, pingClient->sockd, NULL);
	close(pingClient->sockd);
	pingClient->sockd = 0;

	g_queue_free(pingClient->pingTimes);

	return PING_CLIENT_SUCCESS;
}
