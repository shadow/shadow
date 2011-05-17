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

#ifndef SIMPLE_TRANSPORT_LIB_H_
#define SIMPLE_TRANSPORT_LIB_H_

#include <netinet/in.h>
#include <stdio.h>

/* Structure for server-specific data. */
typedef struct server_t {
	int listening_socketd;
} server_t, *server_tp;

/* Structure for client-specific data. */
typedef struct client_t {
	/* empty for now */
} client_t, *client_tp;

/* The main structure that will hold all of my module-specific variables. */
typedef struct {
	in_addr_t ip;
	char ipstring[40];
	int is_server;
	int did_init;
	int num_msgs_sent;
	int num_msgs_received;
	server_tp sdata;
	client_tp cdata;
} simple_transport_t, *simple_transport_tp;

/* Helpful macro for logging. */
#ifndef LOG
#define LOG(fmt, ...) printf("<%s> " fmt, instance->ipstring, ## __VA_ARGS__)
#endif

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 01000000
#endif

#define SERVER_LISTEN_PORT 60000
#define MAX_CONNECTIONS 100
#define TRUE 1
#define FALSE 0
#define ERROR -1

/* Convenience (?) function that calls inet_ntop() with AF_INET. */
const char * ip_to_string(in_addr_t ip, char *buffer, size_t buflen);

/*
 * Open a DGRAM socket in nonblocking mode, bind to SERVER_LISTEN_PORT.
 * returns the socket descriptor, or ERROR if error
 */
int udpserver_start(simple_transport_tp instance);
/*
 * Open a DGRAM socket in nonblocking mode.
 * returns the socket descriptor, or ERROR if error
 */
int udpclient_start(simple_transport_tp instance);

/*
 * Open a STREAM socket in nonblocking mode, bind to SERVER_LISTEN_PORT and listen
 * as a server socket. Accept a connection, if there is one.
 * returns the listening socket descriptor, or ERROR if error
 */
int tcpserver_start(simple_transport_tp instance);
/*
 * Attempts to accept a connection from a client, returning the socket
 * descriptor of the new connection or ERROR if error.
 */
int tcpserver_accept(simple_transport_tp instance);
/*
 * Create a STREAM socket in nonblocking mode and connect to the given address
 * and port.
 * returns the descriptor to the connected socket, or ERROR if error
 */
int tcpclient_start(simple_transport_tp instance, in_addr_t server_address, int server_port);

/*
 * Send a message to the given destination.
 * returns the number of bytes sent, or ERROR if error
 */
int transport_send_message(simple_transport_tp instance, int socketd, struct sockaddr_in* destination);
/*
 * Receive a message on the given socket descriptor, and fill in the address
 * information for the source of the message.
 * returns the number of bytes received, or ERROR if error
 */
int transport_receive_message(simple_transport_tp instance, int socketd, struct sockaddr* source);

#endif /* SIMPLE_TRANSPORT_LIB_H_ */
