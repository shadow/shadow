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

/* This module implements the module_interface */
#include "shd-plugin-interface.h"

/* This module makes calls to the DVN core through the standard network routing interface */
#include "shd-plugin.h"

/* Handy functions and structs for simple transport */
#include "pingpong_lib.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
simple_transport_t instance;

void _plugin_init() {
	snri_log(LOG_INFO, "_plugin_init");
	/* Register the globals here. Since we are storing them in a struct, we
	 * only have to register one (the struct itself). */
	snri_register_globals(1,  sizeof(instance), &instance);
}

void _plugin_uninit() {
	snri_log(LOG_INFO, "_plugin_uninit");
}

void _plugin_instantiate(gint argc, gchar * argv[]) {
	gchar buffer[40];

	/* get IP address through SNRI */
	if(snri_getip(&instance.ip) == SNRICALL_ERROR) {
		snri_log(LOG_ERR, "Error getting IP address!");
		return;
	}
	/* also save IP as string */
	memcpy(instance.ipstring, ip_to_string(instance.ip,buffer,sizeof(buffer)), sizeof(instance.ipstring));

	/* no server name in the args means we are the server */
	if(argc == 0) {
		/* setup and start the server */
		instance.is_server = TRUE;
		instance.sdata = calloc(1, sizeof(server_t));

		if(tcpserver_start(&instance) == ERROR){
			snri_log(LOG_ERR, "Error starting server at %s", instance.ipstring);
			/* cannot continue without a server */
			exit(ERROR);
		} else {
			snri_log(LOG_MSG, "Started server at %s", instance.ipstring);
		}
	} else {
		/* setup and start a client */
		instance.is_server = FALSE;
		instance.cdata = calloc(1, sizeof(client_t));

		in_addr_t server_ip;
		snri_resolve_name(argv[0], &server_ip);

		if(tcpclient_start(&instance, server_ip, htons(SERVER_LISTEN_PORT)) == ERROR){
			snri_log(LOG_ERR, "Error starting client at %s", instance.ipstring);
		} else {
			snri_log(LOG_MSG, "Started client at %s, bootstrapping from server %s", instance.ipstring,
					ip_to_string(server_ip,buffer,sizeof(buffer)));
		}
	}
}

void _plugin_destroy() {
	/* free memory and cleanup */
	if(instance.is_server) {
		free(instance.sdata);
	} else {
		free(instance.cdata);
	}
	snri_log(LOG_INFO, "Module destroyed after sending %i messages and receiving %i messages.",
			instance.num_msgs_sent, instance.num_msgs_received);
}

void _plugin_socket_readable(gint socket){
	snri_log(LOG_INFO, "_plugin_socket_readable for socket %i", socket);

	struct sockaddr_in source;
	source.sin_family = AF_INET;
	gint socketd = socket;

	if(instance.is_server && !instance.did_init) {
		if(socketd == instance.sdata->listening_socketd) {
			/* server needs to accept a connection */
			socketd = tcpserver_accept(&instance);
		}
		instance.did_init = 1;
	}

	/* receive call will tell fill in IP and port we are receiving from */
	if(transport_receive_message(&instance, socketd, (struct sockaddr*) &source) > 0){
		transport_send_message(&instance, socketd, &source);
	}
}

void _plugin_socket_writable(gint socket){
	snri_log(LOG_INFO, "_plugin_socket_writable for socket %i", socket);
	if(!instance.is_server && !instance.did_init) {
		/* client needs to start sending */
		struct sockaddr_in source;
		transport_send_message(&instance, socket, &source);
		instance.did_init = 1;
	}
}
