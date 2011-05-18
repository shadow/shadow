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

#include <netinet/in.h>

/* This module implements the module_interface */
#include "shd-plugin-interface.h"

/* This module makes calls to the shadow core through the standard network routing
 * interface. System socket calls should also be preloaded */
#include "shd-plugin.h"

/* echo server and client functionality */
#include "echo_lib.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
echoloopback_t echoloopback_inst;

void _plugin_init() {
	/* Register the globals here. Since we are storing them in a struct, we
	 * only have to register one (the struct itself). */
	snri_register_globals(1,  sizeof(echoloopback_inst), &echoloopback_inst);
}

void _plugin_uninit() {
}

void _plugin_instantiate(int argc, char * argv[]) {
	echo_server_instantiate(&echoloopback_inst.server, argc, argv, htonl(INADDR_LOOPBACK));
	echo_client_instantiate(&echoloopback_inst.client, argc, argv, htonl(INADDR_LOOPBACK));
}

void _plugin_destroy() {
}

void _plugin_socket_readable(int sockd){
	if(sockd == echoloopback_inst.client.sd) {
		echo_client_socket_readable(&echoloopback_inst.client, sockd);
	} else {
		/* may be the listening socket or its multiplexed child socket */
		echo_server_socket_readable(&echoloopback_inst.server, sockd);
	}
}

void _plugin_socket_writable(int sockd){
	/* server does nothing on writable notification */
	if(sockd == echoloopback_inst.client.sd) {
		echo_client_socket_writable(&echoloopback_inst.client, sockd);
	}
}
