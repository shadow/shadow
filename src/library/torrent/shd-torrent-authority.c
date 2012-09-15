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
	ta->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, torrentAuthority_connection_destroy_cb);
	ta->nodes = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);

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

	TorrentAuthority_Node* node;
	guchar buffer[1024];
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
			if(connection) {
				addr = connection->addr;
			}
			memcpy(&port, &buffer[1], sizeof(port));

			node = g_new0(TorrentAuthority_Node, 1);
			node->addr = addr;
			node->port = port;
			node->sockd = sockd;

			torrentAuthority_changeEpoll(ta, sockd, EPOLLOUT);

			gchar *key = g_new(gchar, 32);
			sprintf(key, "%s:%d",inet_ntoa((struct in_addr){addr}), port);
			g_hash_table_replace(ta->nodes, key, node);

			GList *currKey = g_hash_table_get_keys(ta->nodes);
			while(currKey != NULL) {
				TorrentAuthority_Node *currNode = g_hash_table_lookup(ta->nodes, currKey->data);
				if(currNode->addr != addr || currNode->port != port) {
					gint offset = 1;
					buffer[0] = 1;
					memcpy(buffer + offset, &(node->addr), sizeof(node->addr));
					offset += sizeof(node->addr);
					memcpy(buffer + offset, &(node->port), sizeof(node->port));
					offset += sizeof(node->port);

					bytes = send(currNode->sockd, buffer, offset, 0);
					if(bytes < 0) {
						return TA_ERR_SEND;
					}
				}
				currKey = g_list_next(currKey);
			}
		}

		case TA_MSG_REQUEST_NODES: {
			gint offset = 1;
			GList *nodes = g_hash_table_get_values(ta->nodes);
			gint numNodes = g_list_length(nodes);
			buffer[0] = numNodes - 1;
			for(int i = 0; i < numNodes; i++) {
				node = g_list_nth(nodes, i)->data;
				if(node->sockd != sockd) {
					memcpy(buffer + offset, &(node->addr), sizeof(node->addr));
					offset += sizeof(node->addr);
					memcpy(buffer + offset, &(node->port), sizeof(node->port));
					offset += sizeof(node->port);
				}
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
	g_hash_table_destroy(ta->connections);
	g_hash_table_destroy(ta->nodes);

	epoll_ctl(ta->epolld, EPOLL_CTL_DEL, ta->listenSockd, NULL);
//	close(ta->epolld);
	if(close(ta->listenSockd) < 0) {
		return TS_ERR_CLOSE;
	}

	return TS_SUCCESS;

}

