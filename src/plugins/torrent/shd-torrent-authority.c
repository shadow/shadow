/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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

static void torrentAuthority_log(TorrentAuthority *ta, enum torrentAuthority_loglevel level, const gchar* format, ...) {
	/* if they gave NULL as a callback, dont log */
	if(ta != NULL && ta->log_cb != NULL) {
		va_list vargs, vargs_copy;
		size_t s = sizeof(ta->logBuffer);

		va_start(vargs, format);
		va_copy(vargs_copy, vargs);
		vsnprintf(ta->logBuffer, s, format, vargs);
		va_end(vargs_copy);

		ta->logBuffer[s-1] = '\0';

		(*(ta->log_cb))(level, ta->logBuffer);
	}
}

void torrentAuthority_changeEpoll(TorrentAuthority *ta, gint sockd, gint event) {
	struct epoll_event ev;
	ev.events = event;
	ev.data.fd = sockd;
	epoll_ctl(ta->epolld, EPOLL_CTL_MOD, sockd, &ev);
}

static void torrentAuthority_connectionClose(TorrentAuthority*ta, TorrentAuthority_Connection* connection) {
	epoll_ctl(ta->epolld, EPOLL_CTL_DEL, connection->sockd, NULL);
	g_hash_table_remove(ta->connections, &(connection->sockd));
}

static void torrentAuthority_connection_destroy_cb(gpointer data) {
	TorrentAuthority_Connection *conn = data;

	if(conn != NULL) {
		close(conn->sockd);
		free(conn);
	}
}

gint torrentAuthority_start(TorrentAuthority* ta, gint epolld, in_addr_t listenIP, in_port_t listenPort, gint maxConnections) {
	if(ta == NULL) {
		return TA_ERR_INVALID;
	}

	/* create the socket and get a socket descriptor */
	gint sockd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if (sockd == -1) {
		return TA_ERR_SOCKET;
	}

	/* setup the socket address info, client has outgoing connection to server */
	struct sockaddr_in listener;
	memset(&listener, 0, sizeof(listener));
	listener.sin_family = AF_INET;
	listener.sin_addr.s_addr = listenIP;
	listener.sin_port = listenPort;

	/* bind the socket to the server port */
	gint result = bind(sockd, (struct sockaddr *) &listener, sizeof(listener));
	if (result == -1) {
		return TA_ERR_BIND;
	}

	/* set as server socket that will listen for clients */
	result = listen(sockd, maxConnections);
	if (result == -1) {
		return TA_ERR_LISTEN;
	}

	/* create our server and store our server socket */
	memset(ta, 0, sizeof(TorrentAuthority));
	ta->listenSockd = sockd;
	ta->epolld = epolld;
	ta->servers = NULL;
	ta->clients = NULL;
	ta->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, torrentAuthority_connection_destroy_cb);
//	ta->nodes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = ta->listenSockd;
	if(epoll_ctl(ta->epolld, EPOLL_CTL_ADD, ta->listenSockd, &ev) < 0) {
		return TA_ERR_EPOLL;
	}

	return 0;
}

gint torrentAuthority_activate(TorrentAuthority* ta, gint sockd) {
	if(ta == NULL || sockd < 0) {
		return TA_ERR_FATAL;
	}

	if(sockd == ta->listenSockd) {
		gint res = 0;
		do {
			res = torrentAuthority_accept(ta, NULL);
		} while(!res);
		return res;
	}

	/* otherwise check for a connections */
	TorrentAuthority_Connection* connection = g_hash_table_lookup(ta->connections, &sockd);
	if(connection == NULL) {
		return TA_ERR_NOCONN;
	}

	guchar buffer[4096];
	ssize_t bytes = recv(sockd, buffer, sizeof(buffer), 0);
	if(bytes < 0) {
		if(errno == EWOULDBLOCK) {
			return TA_ERR_WOULDBLOCK;
		} else {
			torrentAuthority_connectionClose(ta, connection);
			return TA_ERR_RECV;
		}
	} else if(bytes == 0) {
		torrentAuthority_connectionClose(ta, connection);
		return TA_CLOSED;
	}

	in_addr_t addr = 0;
	in_port_t port;

	switch(buffer[0]) {
		case TA_MSG_REGISTER: {
			addr = connection->addr;
			memcpy(&port, &buffer[1], sizeof(port));
			connection->serverPort = port;

			ta->servers = g_list_append(ta->servers, connection);
			//torrentAuthority_changeEpoll(ta, sockd, EPOLLOUT);

			for(GList *iter = ta->clients; iter; iter = g_list_next(iter)) {
				TorrentAuthority_Connection *client = iter->data;
				gint offset = 1;
				buffer[0] = 1;
				memcpy(buffer + offset, &addr, sizeof(addr));
				offset += sizeof(addr);
				memcpy(buffer + offset, &port, sizeof(port));
				offset += sizeof(port);

				bytes = send(client->sockd, buffer, offset, 0);
				if(bytes < 0) {
					return TA_ERR_SEND;
				}
			}
			break;
		}

		case TA_MSG_REQUEST_NODES: {
			ta->clients = g_list_append(ta->clients, connection);

			buffer[0] = g_list_length(ta->servers);
			gint offset = 1;
			for(GList *iter = ta->servers; iter; iter = g_list_next(iter)) {
				TorrentAuthority_Connection *server = iter->data;
				memcpy(buffer + offset, &(server->addr), sizeof(server->addr));
				offset += sizeof(server->addr);
				memcpy(buffer + offset, &(server->serverPort), sizeof(server->serverPort));
				offset += sizeof(server->serverPort);
			}

			bytes = send(sockd, buffer, offset, 0);
			if(bytes < 0) {
				return TA_ERR_SEND;
			}
			torrentAuthority_changeEpoll(ta, sockd, EPOLLIN);
			break;
		}

		default:
			break;
	}

	return 0;
}

gint torrentAuthority_accept(TorrentAuthority* ta, gint* sockdOut) {
	if(ta == NULL) {
		return -3;
	}

	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	/* try to accept a connection */
	gint sockd = accept(ta->listenSockd, (struct sockaddr *)&addr, &addrlen);
	if(sockd < 0) {
		if(errno == EWOULDBLOCK) {
			return TA_ERR_WOULDBLOCK;
		} else {
			return TA_ERR_ACCEPT;
		}
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = sockd;
	if(epoll_ctl(ta->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		perror("epoll_ctl");
		return TA_ERR_EPOLL;
	}

	TorrentAuthority_Connection *connection = g_new0(TorrentAuthority_Connection, 1);
	connection->sockd = sockd;
	connection->addr = addr.sin_addr.s_addr;

	g_hash_table_replace(ta->connections, &(connection->sockd), connection);
	if(sockdOut != NULL) {
		*sockdOut = sockd;
	}
	return 0;
}

gint torrentAuthority_shutdown(TorrentAuthority* ta) {
	/* destroy the hashtable. this calls the connection destroy function for each. */
	g_list_free(ta->servers);
	g_list_free(ta->clients);
	g_hash_table_destroy(ta->connections);

	epoll_ctl(ta->epolld, EPOLL_CTL_DEL, ta->listenSockd, NULL);
	if(close(ta->listenSockd) < 0) {
		return TS_ERR_CLOSE;
	}

	return TS_SUCCESS;

}

