/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "snricall_codes.h"
#include "shd-plugin.h"
#include "echo_lib.h"

void echo_client_instantiate(echoclient_tp ec, gint argc, gchar * argv[], in_addr_t bootstrap_address) {
	if(ec == NULL) {
		snri_log(LOG_WARN, "echo_client_instantiate called with NULL client\n");
		return;
	}

	snri_log(LOG_INFO, "echo_client_instantiate\n");

	/* clear echoclient_inst vars */
	memset(ec, 0, sizeof(echoclient_t));

	gint sockd = 0;
	struct sockaddr_in server;

	/* setup the socket address info, client has outgoing connection to server */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = bootstrap_address;
	server.sin_port = htons(ECHO_SERVER_PORT);

	/* create the socket and get a socket descriptor */
	if ((sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("echo_client_instantiate: Error in tcpclient_start: socket");
	}

	/* connect to server. since we cannot block, shadow will notify us via
	 * _module_socket_writable when the connection is established
	 */
	if (!(connect(sockd,(struct sockaddr *)  &server, sizeof(server)) == ERROR
			&& errno == EINPROGRESS)) {
		perror("echo_client_instantiate: Error in tcpclient_start: connect");
	}

	ec->sd = sockd;
}

void echo_client_socket_readable(echoclient_tp ec, gint sockd) {
	if(ec == NULL) {
		snri_log(LOG_WARN, "echo_client_socket_readable called with NULL client\n");
		return;
	}

	snri_log(LOG_INFO, "echo_client_socket_readable for socket %i\n", sockd);

	if(!ec->is_done) {
		ssize_t b = 0;
		while(ec->amount_sent-ec->recv_offset > 0 &&
				(b = read(sockd, ec->recv_buffer+ec->recv_offset, ec->amount_sent-ec->recv_offset)) > 0) {
			snri_log(LOG_INFO, "client socket %i read %i bytes: '%s'\n", sockd, b, ec->recv_buffer+ec->recv_offset);
			ec->recv_offset += b;
		}

		if(ec->recv_offset >= ec->amount_sent) {
			ec->is_done = 1;
			if(memcmp(ec->send_buffer, ec->recv_buffer, ec->amount_sent)) {
				snri_log(LOG_WARN, "inconsistent echo received!\n");
			} else {
				snri_log(LOG_MSG, "consistent echo received!\n");
			}
			close(sockd);
		} else {
			snri_log(LOG_INFO, "echo progress: %i of %i bytes\n", ec->recv_offset, ec->amount_sent);
		}
	}
}

/* fills buffer with size random gcharacters */
static void fill_gchar_buffer(gchar* buffer, gint size) {
	for(gint i = 0; i < size; i++) {
		gint n = rand() % 26;
		buffer[i] = 'a' + n;
	}
}

void echo_client_socket_writable(echoclient_tp ec, gint sockd) {
	if(ec == NULL) {
		snri_log(LOG_WARN, "echo_client_socket_writable called with NULL client\n");
		return;
	}

	snri_log(LOG_INFO, "echo_client_socket_writable for socket %i\n", sockd);

	if(!ec->sent_msg) {
		fill_gchar_buffer(ec->send_buffer, sizeof(ec->send_buffer)-1);
		ssize_t b = write(sockd, ec->send_buffer, sizeof(ec->send_buffer));
		ec->sent_msg = 1;
		ec->amount_sent = b;
		snri_log(LOG_INFO, "client socket %i wrote %i bytes: '%s'\n", sockd, b, ec->send_buffer);
	}
}

void echo_server_instantiate(echoserver_tp es, gint argc, gchar * argv[], in_addr_t bind_address) {
	if(es == NULL) {
		snri_log(LOG_WARN, "echo_server_instantiate called with NULL server\n");
		return;
	}

	snri_log(LOG_INFO, "echo_server_instantiate\n");
	es->listen_sd = 0;

	/* start up the echo server */
	gint socketd;
	struct sockaddr_in server;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("echo_server_instantiate: error creating socket");
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = bind_address;
	server.sin_port = htons(ECHO_SERVER_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &server, sizeof(server)) == ERROR) {
		perror("echo_server_instantiate: error in bind");
	}

	/* set as server socket */
	if (listen(socketd, 100) == ERROR) {
		perror("echo_server_instantiate: error in listen");
	}

	memset(es, 0, sizeof(echoserver_t));

	/* store the socket as our listening socket */
	es->listen_sd = socketd;
}

void echo_server_socket_readable(echoserver_tp es, gint sockd) {
	if(es == NULL) {
		snri_log(LOG_WARN, "echo_server_socket_readable called with NULL server\n");
		return;
	}

	snri_log(LOG_INFO, "echo_server_socket_readable for socket %i\n", sockd);

	if(sockd == es->listen_sd) {
		/* need to accept a connection on server listening socket,
		 * dont care about address of connector.
		 * this gives us a new socket thats connected to the client */
		if((sockd = accept(es->listen_sd, NULL, NULL)) == ERROR) {
			perror("echo_server_socket_readable: error accepting socket");
		}
	}

	/* read all data available */
	gint read_size = BUFFERSIZE - es->read_offset;
	ssize_t bread;
	while(read_size > 0 &&
			(bread = read(sockd, es->echo_buffer + es->read_offset, read_size)) > 0) {
		snri_log(LOG_INFO, "server socket %i read %i bytes\n", sockd, (gint)bread);
		es->read_offset += bread;
		read_size -= bread;
	}

	/* echo it back to the client on the same sd,
	 * also taking care of data that is still hanging around from previous reads. */
	gint write_size = es->read_offset - es->write_offset;
	ssize_t bwrote;
	while(write_size > 0 &&
			(bwrote = write(sockd, es->echo_buffer + es->write_offset, write_size)) > 0) {
		snri_log(LOG_INFO, "server socket %i wrote %i bytes\n", sockd, (gint)bwrote);
		es->write_offset += bwrote;
		write_size -= bwrote;
	}

	/* cant close sockd to client if we havent received everything yet.
	 * keep it simple and just keep the socket open for now.
	 */
}
