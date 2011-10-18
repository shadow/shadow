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

PluginFunctionTable pingpong_pluginFunctions = {
	&pingpong_new, &pingpong_free, &pingpong_readable, &pingpong_writable,
};

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
PingPong pingpong_data;

void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	/* initialize */
	memset(&pingpong_data, 0, sizeof(PingPong));

	/* save the shadow functions we will use */
	pingpong_data.shadowlibFuncs = shadowlibFuncs;

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 */
	gboolean success = shadowlibFuncs->registration(&pingpong_pluginFunctions, 1, sizeof(PingPong), &pingpong_data);
	if(success) {
		shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered pingpong plug-in state");
	} else {
		shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "error registering pingpong plug-in state");
	}
}

void pingpong_new(int argc, char* argv[]) {
	pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "pingpong_new called");

	pingpong_data.client = NULL;
	pingpong_data.server = NULL;

	char* USAGE = "PingPong usage: 'client tcp serverHostname', 'client udp serverHostname', 'server tcp' or 'server udp'";
	if(argc < 1) {
		pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
		return;
	}

	/* parse command line args */
	char* mode = argv[0];

	if(g_strcasecmp(mode, "client") == 0) {
		if(argc < 3) {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
			return;
		}

		/* start up a client, connecting to the server specified in args */
		gchar* protocol = argv[1];
		gchar* serverHostname = argv[2];
		pingpong_data.client = pingpongclient_new(protocol, serverHostname, pingpong_data.shadowlibFuncs);
	} else if(g_strcasecmp(mode, "server") == 0) {
		if(argc < 2) {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
			return;
		}

		/* start up a server */
		char* protocol = argv[1];
		pingpong_data.server = pingpongserver_new(protocol, pingpong_data.shadowlibFuncs);
	} else {
		pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, USAGE);
	}
}

void pingpong_free() {
	pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "pingpong_free called");
	if(pingpong_data.client) {
		pingpongclient_free(pingpong_data.client);
	}
	if(pingpong_data.server) {
		pingpongserver_free(pingpong_data.server);
	}
}

void pingpong_readable(int socketDesriptor) {
	pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "pingpong_readable called");
	if(pingpong_data.client) {
		pingpongclient_readable(pingpong_data.client, socketDesriptor);
	}
	if(pingpong_data.server) {
		pingpongserver_readable(pingpong_data.server, socketDesriptor);
	}
}

void pingpong_writable(int socketDesriptor) {
	pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "pingpong_writable called");
	if(pingpong_data.client) {
		pingpongclient_writable(pingpong_data.client, socketDesriptor);
	}
	if(pingpong_data.server) {
		pingpongserver_writable(pingpong_data.server, socketDesriptor);
	}
}

gint pingpong_sendMessage(gint socketd, struct sockaddr_in* destination) {
	gchar* message;
	gint result = 0;

	if (pingpong_data.server) {
		message = "Server PONG!";
	} else {
		message = "Client PING!";
	}

	/* send a message through the socket to the destination address and port */
	result = sendto(socketd, message, strlen(message), 0,(struct sockaddr *) destination, sizeof(*destination));
	if (result == ERROR) {
		/* EAGAIN or EWOULDBLOCK are valid for non-blocking sockets */
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "blocked on sending, try again later");
			result = 0;
		} else {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in sendto");
		}
	} else {
		pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "Sent '%s' to %s:%i.", message, inet_ntoa((struct in_addr){destination->sin_addr.s_addr}), ntohs(destination->sin_port));
	}

	return result;
}

gint pingpong_receiveMessage(gint socketd, struct sockaddr* source) {
	gint result = 0;

	/* receive if there is data available */
	gpointer data = calloc(1, 256);

	socklen_t source_len = sizeof(*source);
	result = recvfrom(socketd, data, 255, 0, source, &source_len);
	if(result == ERROR){
		/* EAGAIN or EWOULDBLOCK are valid for non-blocking sockets */
		if(errno == EAGAIN || errno == EWOULDBLOCK) {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "No data to receive, will try again on next receive call");
			result = 0;
		} else {
			pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in recvfrom");
		}
	} else {
		struct sockaddr_in* sa = (struct sockaddr_in*) source;
		pingpong_data.shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "Received '%s' from %s:%i.", (gchar*)data, inet_ntoa((struct in_addr){sa->sin_addr.s_addr}),  ntohs(sa->sin_port));
	}

	free(data);

	return result;
}
