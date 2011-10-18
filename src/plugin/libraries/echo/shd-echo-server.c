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

#include "shd-echo.h"

EchoServer* echoserver_new(in_addr_t bindIPAddress, ShadowlibLogFunc log) {
	/* start up the echo server */
	gint socketd;
	struct sockaddr_in server;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error creating socket");
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = bindIPAddress;
	server.sin_port = htons(ECHO_SERVER_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &server, sizeof(server)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in bind");
	}

	/* set as server socket */
	if (listen(socketd, 100) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in listen");
	}

	/* store the socket as our listening socket */
	EchoServer* es = g_new0(EchoServer, 1);
	es->listen_sd = socketd;
	return es;
}

void echoserver_free(EchoServer* es) {
	g_assert(es);
	g_free(es);
}

void echoserver_socketReadable(EchoServer* es, gint sockd, ShadowlibLogFunc log) {
	if(es == NULL) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "NULL server");
		return;
	}

	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "trying to read socket %i", sockd);

	if(sockd == es->listen_sd) {
		/* need to accept a connection on server listening socket,
		 * dont care about address of connector.
		 * this gives us a new socket thats connected to the client */
		if((sockd = accept(es->listen_sd, NULL, NULL)) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error accepting socket");
		}
	}

	/* read all data available */
	gint read_size = BUFFERSIZE - es->read_offset;
	ssize_t bread;
	while(read_size > 0 &&
			(bread = read(sockd, es->echo_buffer + es->read_offset, read_size)) > 0) {
		log(G_LOG_LEVEL_INFO, __FUNCTION__, "server socket %i read %i bytes", sockd, (gint)bread);
		es->read_offset += bread;
		read_size -= bread;
	}

	/* echo it back to the client on the same sd,
	 * also taking care of data that is still hanging around from previous reads. */
	gint write_size = es->read_offset - es->write_offset;
	ssize_t bwrote;
	while(write_size > 0 &&
			(bwrote = write(sockd, es->echo_buffer + es->write_offset, write_size)) > 0) {
		log(G_LOG_LEVEL_INFO, __FUNCTION__, "server socket %i wrote %i bytes", sockd, (gint)bwrote);
		es->write_offset += bwrote;
		write_size -= bwrote;
	}

	/* cant close sockd to client if we havent received everything yet.
	 * keep it simple and just keep the socket open for now.
	 */
}
