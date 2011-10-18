/*
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

static gint _pingpongclient_startTCP(PingPongClient* client, in_addr_t serverIP, in_port_t serverPort) {
	g_assert(client);

	gint socketd;
	struct sockaddr_in server;

	/* setup the socket address info, client has outgoing connection to server */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = serverIP;
	server.sin_port = serverPort;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		client->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in socket");
		return ERROR;
	}

	/* connect to server. since we cannot block, DVN will notify us via
	 * _module_socket_writable when the connection is established
	 */
	if (!(connect(socketd,(struct sockaddr *)  &server, sizeof(server)) == ERROR
			&& errno == EINPROGRESS)) {
		client->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in connect");
		return ERROR;
	}

	return socketd;
}

static gint _pingpongclient_startUDP(PingPongClient* client) {
	gint socketd;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == ERROR) {
		client->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in socket");
		return ERROR;
	}

	return socketd;
}

PingPongClient* pingpongclient_new(gchar* protocol, gchar* serverHostname,
		ShadowlibFunctionTable* shadowlib)
{
	g_assert(protocol && serverHostname && shadowlib);

	PingPongClient* client = g_new0(PingPongClient, 1);
	client->shadowlib = shadowlib;
	client->serverIP = client->shadowlib->resolveHostname(serverHostname);

	/* parse the arguments */
	if(g_strcasecmp(protocol, "tcp") == 0) {
		client->isTCP = 1;

		client->socketDescriptor = _pingpongclient_startTCP(client, client->serverIP, htons(SERVER_LISTEN_PORT));
		if(client->socketDescriptor == ERROR){
			goto err;
		}
	} else if(g_strcasecmp(protocol, "udp") == 0) {
		client->socketDescriptor = _pingpongclient_startUDP(client);
		if(client->socketDescriptor == ERROR){
			goto err;
		}

		/* start sending data since we do not have to wait for connection */
		pingpongclient_writable(client, client->socketDescriptor);
	} else {
		goto err;
	}

	client->shadowlib->log(G_LOG_LEVEL_INFO, __FUNCTION__, "pinging client created targeting server '%s'", serverHostname);
	return client;

err:
	client->shadowlib->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error creating pingpong client");
	g_free(client);
	return NULL;
}

void pingpongclient_free(PingPongClient* client) {
	g_assert(client);
	client->shadowlib->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "pingpong client sent %i pings and received %i pongs", client->nPingsSent, client->nPongsReceived);
	g_free(client);
}

void pingpongclient_readable(PingPongClient* client, gint socketDescriptor) {
	struct sockaddr_in source;
	source.sin_family = AF_INET;
	/* receive call will fill in IP and port we are receiving from */
	if(pingpong_receiveMessage(socketDescriptor, (struct sockaddr*) &source) > 0) {
		client->isPinging = FALSE;
		client->nPongsReceived++;
		/* send a reply back to the source */
		pingpongclient_writable(client, socketDescriptor);
	}
}

void pingpongclient_writable(PingPongClient* client, gint socketDescriptor) {
	g_assert(client);

	if(!client->isPinging) {
		client->shadowlib->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "pinging server %s", inet_ntoa((struct in_addr){client->serverIP}));
		struct sockaddr_in server;
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = client->serverIP;
		server.sin_port = htons(SERVER_LISTEN_PORT);
		if(pingpong_sendMessage(socketDescriptor, &server) > 0) {
			client->isPinging = TRUE;
			client->nPingsSent++;
		}
	}
}
