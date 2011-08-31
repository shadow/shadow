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

#include "pingpong_lib.h"

const gchar * ip_to_string(in_addr_t ip, gchar *buffer, size_t buflen) {
	return inet_ntop(AF_INET, &ip, buffer, buflen);
}

gint udpserver_start(simple_transport_tp instance){
	gint socketd;
	struct sockaddr_in server;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("Error in udpserver_start: socket");
		return ERROR;
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SERVER_LISTEN_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &server, sizeof(server)) == ERROR) {
		perror("Error in server_start: bind");
		return ERROR;
	}

	return socketd;
}

gint udpclient_start(simple_transport_tp instance){
	gint socketd;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("Error in udpclient_start: socket");
		return ERROR;
	}

	return socketd;
}

gint tcpserver_start(simple_transport_tp instance){
	gint socketd;
	struct sockaddr_in server;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("Error in tcpserver_start: socket");
		return ERROR;
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(SERVER_LISTEN_PORT);

	/* bind the socket to the server port */
	if (bind(socketd, (struct sockaddr *) &server, sizeof(server)) == ERROR) {
		perror("Error in tcpserver_start: bind");
		return ERROR;
	}

	/* set as server socket */
	if (listen(socketd, MAX_CONNECTIONS) == ERROR) {
		perror("Error in tcpserver_start: listen");
		return ERROR;
	}

	/* Normally we would call accept to wait for a client connection. Since DVN
	 * does not support blocking calls, it will act as if we set the socket to
	 * non-blocking mode (SOCK_NONBLOCK) and return -1 with errno
	 * set to EAGAIN. We should not have any connections to accept yet.
	 */

	/* store the socket as our listening socket */
	instance->sdata->listening_socketd = socketd;
	return socketd;
}

gint tcpserver_accept(simple_transport_tp instance){
	/* need to accept a connection on server listening socket */
	struct sockaddr_in client;
	gint client_len = sizeof(client);

	gint socketd_to_client = accept(instance->sdata->listening_socketd, (struct sockaddr *) &client, (socklen_t *) &client_len);
	if(socketd_to_client == ERROR) {
		perror("Error in tcpserver_accept: accept");
	}
	return socketd_to_client;
}

gint tcpclient_start(simple_transport_tp instance, in_addr_t server_address, gint server_port){
	gint socketd;
	struct sockaddr_in server;

	/* setup the socket address info, client has outgoing connection to server */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = server_address;
	server.sin_port = server_port;

	/* create the socket and get a socket descriptor */
	if ((socketd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		perror("Error in tcpclient_start: socket");
		return ERROR;
	}

	/* connect to server. since we cannot block, DVN will notify us via
	 * _module_socket_writable when the connection is established
	 */
	if (!(connect(socketd,(struct sockaddr *)  &server, sizeof(server)) == ERROR
			&& errno == EINPROGRESS)) {
		perror("Error in tcpclient_start: connect");
		return ERROR;
	}

	return socketd;
}

gint transport_send_message(simple_transport_tp instance, gint socketd, struct sockaddr_in* destination) {
	gchar* message;
	gint result = 0;

	if (instance->is_server) {
		message = "Server PONG!";
	} else {
		message = "Client PING!";
	}

	/* send a message through the socket to the destination address and port */
	result = sendto(socketd, message, strlen(message), 0,(struct sockaddr *) destination, sizeof(*destination));
	if (result == ERROR) {
		perror("Error in transport_send_message: sendto");
	} else {
		gchar buffer[40];
		LOG("Sent '%s' to %s:%i.\n", message, ip_to_string(destination->sin_addr.s_addr, buffer, sizeof(buffer)), destination->sin_port);
		instance->num_msgs_sent++;
	}

	return result;
}

gint transport_receive_message(simple_transport_tp instance, gint socketd, struct sockaddr* source) {
	gint result = 0;

	/* receive if there is data available */
	gpointer data = calloc(1, 256);
	gchar buffer[40];

	socklen_t source_len = sizeof(*source);
	result = recvfrom(socketd, data, 255, 0, source, &source_len);
	if(result == ERROR){
		/* EAGAIN or EWOULDBLOCK are valid for non-blocking sockets */
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			LOG("No data to receive, will try again on next receive call\n");
			result = 0;
		} else {
			perror("Error in transport_receive_message: recvfrom");
		}
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*) source;
		LOG("Received '%s' from %s:%i.\n", (gchar*)data, ip_to_string(sa->sin_addr.s_addr, buffer, sizeof(buffer)), sa->sin_port);
		instance->num_msgs_received++;
	}

	free(data);

	return result;
}
