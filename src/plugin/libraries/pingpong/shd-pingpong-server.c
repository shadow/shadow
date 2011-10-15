/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shd-pingpong.h"

static gint _pingpongserver_startTCP(PingPongServer* server) {
	gint socketd;
	struct sockaddr_in addrin;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in socket");
		return ERROR;
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&addrin, 0, sizeof(addrin));
	addrin.sin_family = AF_INET;
	addrin.sin_addr.s_addr = INADDR_ANY;
	addrin.sin_port = htons(SERVER_LISTEN_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &addrin, sizeof(addrin)) == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in bind");
		return ERROR;
	}

	/* set as server socket */
	if (listen(socketd, MAX_CONNECTIONS) == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in listen");
		return ERROR;
	}

	/* Normally we would call accept to wait for a client connection. Since we
	 * do not support blocking calls, it will act as if we set the socket to
	 * non-blocking mode (SOCK_NONBLOCK) and return -1 with errno
	 * set to EAGAIN. We should not have any connections to accept yet.
	 */

	/* store the socket as our listening socket */
	server->listenSocketDescriptor = socketd;
	return socketd;
}

static gint _pingpongserver_acceptTCP(PingPongServer* server) {
	/* need to accept a connection on server listening socket */
	struct sockaddr_in client;
	gint client_len = sizeof(client);

	gint acceptedSocketDescriptor = accept(server->listenSocketDescriptor, (struct sockaddr *) &client, (socklen_t *) &client_len);
	if(acceptedSocketDescriptor == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in accept");
	}
	return acceptedSocketDescriptor;
}

static gint _pingpongserver_startUDP(PingPongServer* server) {
	gint socketd;
	struct sockaddr_in addrin;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in socket");
		return ERROR;
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&addrin, 0, sizeof(addrin));
	addrin.sin_family = AF_INET;
	addrin.sin_addr.s_addr = INADDR_ANY;
	addrin.sin_port = htons(SERVER_LISTEN_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &addrin, sizeof(addrin)) == ERROR) {
		server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in server_start: bind");
		return ERROR;
	}

	return socketd;
}

PingPongServer* pingpongserver_new(gchar* protocol, ShadowlibFunctionTable* shadowlib)
{
	g_assert(protocol && shadowlib);

	PingPongServer* server = g_new0(PingPongServer, 1);
	server->shadowlib = shadowlib;

	/* parse the arguments */
	if(g_strcasecmp(protocol, "tcp") == 0) {
		server->isTCP = 1;

		server->listenSocketDescriptor = _pingpongserver_startTCP(server);
		if(server->listenSocketDescriptor == ERROR){
			goto err;
		}
	} else if(g_strcasecmp(protocol, "udp") == 0) {
		server->listenSocketDescriptor = _pingpongserver_startUDP(server);
		if(server->listenSocketDescriptor == ERROR){
			goto err;
		}
	} else {
		goto err;
	}

	server->shadowlib->log(G_LOG_LEVEL_INFO, __FUNCTION__, "pingpong server created");
	return server;

err:
	server->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error creating pinpong server");
	g_free(server);
	return NULL;
}

void pingpongserver_free(PingPongServer* server) {
	g_assert(server);
	server->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "pingpong server received %i pings and sent %i pongs", server->nPingsReceived, server->nPongsSent);
	g_free(server);
}

void pingpongserver_readable(PingPongServer* server, gint socketDescriptor) {
	g_assert(server);

	struct sockaddr_in source;
	source.sin_family = AF_INET;
	gint sockd = socketDescriptor;

	if(server->isTCP && !server->isAccepted &&
			server->listenSocketDescriptor == socketDescriptor) {
		/* server needs to accept a connection */
		sockd = _pingpongserver_acceptTCP(server);
		server->isAccepted = TRUE;
	}

	/* receive call will fill in IP and port we are receiving from */
	if(pingpong_receiveMessage(sockd, (struct sockaddr*) &source) > 0){
		server->nPingsReceived++;
		if(pingpong_sendMessage(sockd, &source) > 0) {
			server->nPongsSent++;
		} else {
			server->pongIsBlocked = TRUE;
			server->blockedDescriptor = sockd;
			server->blockedAddress = source.sin_addr.s_addr;
			server->blockedPort = source.sin_port;
		}
	}
}

void pingpongserver_writable(PingPongServer* server, gint socketDescriptor) {
	g_assert(server);
	if(server->pongIsBlocked && (socketDescriptor == server->blockedDescriptor)) {
		struct sockaddr_in dest;
		dest.sin_family = AF_INET;
		dest.sin_addr.s_addr = server->blockedAddress;
		dest.sin_port = server->blockedPort;
		gint sockd = socketDescriptor;
		if(pingpong_sendMessage(sockd, &dest) > 0) {
			server->nPongsSent++;
			server->pongIsBlocked = FALSE;
			server->blockedDescriptor = 0;
			server->blockedAddress = 0;
			server->blockedPort = 0;
		}
	}
}
