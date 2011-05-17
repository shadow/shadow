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

#ifndef ECHO_LIB_H_
#define ECHO_LIB_H_

#include <netinet/in.h>

#define ERROR -1
#define BUFFERSIZE 20000
#define ECHO_SERVER_PORT 60000

typedef struct echoclient_s {
	int sd;
	char send_buffer[BUFFERSIZE];
	char recv_buffer[BUFFERSIZE];
	int recv_offset;
	int sent_msg;
	int amount_sent;
	int is_done;
} echoclient_t, *echoclient_tp;

typedef struct echoserver_s {
	int listen_sd;
} echoserver_t, *echoserver_tp;

typedef struct echoloopback_s {
	echoserver_t server;
	echoclient_t client;
} echoloopback_t, *echoloopback_tp;

void echo_client_instantiate(echoclient_tp ec, int argc, char * argv[], in_addr_t bootstrap_address);
void echo_client_socket_readable(echoclient_tp ec, int sockd);
void echo_client_socket_writable(echoclient_tp ec, int sockd);

void echo_server_instantiate(echoserver_tp es, int argc, char * argv[], in_addr_t bootstrap_address);
void echo_server_socket_readable(echoserver_tp es, int sockd);

#endif /* ECHO_LIB_H_ */
