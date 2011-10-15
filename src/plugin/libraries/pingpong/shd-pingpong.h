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

#ifndef SHD_PINGPONG_H_
#define SHD_PINGPONG_H_

#include <glib.h>
#include <shadowlib.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define SERVER_LISTEN_PORT 60000
#define MAX_CONNECTIONS 100
#define ERROR -1

/* Structure for server-specific data. */
typedef struct _PingPongServer PingPongServer;
struct _PingPongServer {
	ShadowlibFunctionTable* shadowlib;
	gint listenSocketDescriptor;
	gboolean isTCP;
	gboolean isAccepted;
	guint nPingsReceived;
	guint nPongsSent;
	gboolean pongIsBlocked;
	in_addr_t blockedAddress;
	in_port_t blockedPort;
	gint blockedDescriptor;
};

/* Structure for client-specific data. */
typedef struct _PingPongClient PingPongClient;
struct _PingPongClient {
	ShadowlibFunctionTable* shadowlib;
	in_addr_t serverIP;
	gboolean isTCP;
	gint socketDescriptor;
	gboolean isPinging;
	guint nPingsSent;
	guint nPongsReceived;
};

/* The main structure that will hold all of my module-specific variables. */
typedef struct _PingPong PingPong;
struct _PingPong {
	PingPongServer* server;
	PingPongClient* client;
	ShadowlibFunctionTable* shadowlibFuncs;
};

void pingpong_new(int argc, char* argv[]);
void pingpong_free();
void pingpong_readable(int socketDesriptor);
void pingpong_writable(int socketDesriptor);

PingPongClient* pingpongclient_new(gchar* protocol, gchar* serverHostname,
		ShadowlibFunctionTable* shadowlib);
void pingpongclient_free(PingPongClient* client);
void pingpongclient_readable(PingPongClient* client, gint socketDescriptor);
void pingpongclient_writable(PingPongClient* client, gint socketDescriptor);

PingPongServer* pingpongserver_new(gchar* protocol, ShadowlibFunctionTable* shadowlib);
void pingpongserver_free(PingPongServer* server);
void pingpongserver_readable(PingPongServer* server, gint socketDescriptor);
void pingpongserver_writable(PingPongServer* server, gint socketDescriptor);

gint pingpong_sendMessage(gint socketd, struct sockaddr_in* destination);
gint pingpong_receiveMessage(gint socketd, struct sockaddr* source);

#endif /* SHD_PINGPONG_H_ */
