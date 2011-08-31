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
echoclient_t echoclient_inst;

void _plugin_init() {
	/* Register the globals here. Since we are storing them in a struct, we
	 * only have to register one (the struct itself). */
	snri_register_globals(1,  sizeof(echoclient_inst), &echoclient_inst);
}

void _plugin_uninit() {
}

void _plugin_instantiate(gint argc, gchar * argv[]) {
	in_addr_t echo_server_ip;
	snri_resolve_name(argv[0], &echo_server_ip);
	echo_client_instantiate(&echoclient_inst, argc, argv, echo_server_ip);
}

void _plugin_destroy() {
}

void _plugin_socket_readable(gint sockd){
	echo_client_socket_readable(&echoclient_inst, sockd);
}

void _plugin_socket_writable(gint sockd){
	echo_client_socket_writable(&echoclient_inst, sockd);
}
