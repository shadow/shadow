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

#ifndef SHD_ECHO_H_
#define SHD_ECHO_H_

#include <glib.h>
#include <shd-library.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>

#define ERROR -1
#define BUFFERSIZE 20000
#define ECHO_SERVER_PORT 9999
#define MAX_EVENTS 10

enum EchoProtocol {
	EchoNONE, EchoTCP, EchoUDP, EchoPIPE,
};

typedef struct _EchoClient EchoClient;
struct _EchoClient {
	enum EchoProtocol protocol;
	in_addr_t serverIPAddress;
	gint epollFileDescriptor;
	gint sd;
	gchar send_buffer[BUFFERSIZE];
	gchar recv_buffer[BUFFERSIZE];
	gint recv_offset;
	gint sent_msg;
	gint amount_sent;
	gint is_done;
};

typedef struct _EchoServer EchoServer;
struct _EchoServer {
	enum EchoProtocol protocol;
	gint epollFileDescriptor;
	gint listen_sd;
	struct sockaddr_in address;
	gchar echo_buffer[BUFFERSIZE];
	gint read_offset;
	gint write_offset;
};

typedef struct _Echo Echo;
struct _Echo {
	EchoServer* server;
	EchoClient* client;
	ShadowlibFunctionTable* shadowlibFuncs;
} echoloopback_t, *echoloopback_tp;

void echo_new(int argc, char* argv[]);
void echo_free();
void echo_ready();

EchoClient* echoclient_new(enum EchoProtocol protocol, in_addr_t serverIPAddress, ShadowlibLogFunc log);
void echoclient_free(EchoClient* ec);
void echoclient_ready(EchoClient* ec, ShadowlibLogFunc log);

EchoServer* echoserver_new(enum EchoProtocol protocol, in_addr_t bindIPAddress, ShadowlibLogFunc log);
void echoserver_free(EchoServer* es);
void echoserver_ready(EchoServer* es, ShadowlibLogFunc log);

#endif /* SHD_ECHO_H_ */
