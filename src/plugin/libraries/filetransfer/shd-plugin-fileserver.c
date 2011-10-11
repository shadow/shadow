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
#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <arpa/inet.h>

/* This module implements the module_interface */
#include "shd-plugin-interface.h"

/* This module makes calls to the DVN core through the standard network routing
 * interface. */
#include "shd-plugin.h"

/* this plugin implements a fileserver */
#include "shd-filetransfer.h"

typedef struct plugin_fileserver_s {
	fileserver_t fs;
} plugin_fileserver_t, *plugin_fileserver_tp;

/* my global structure to hold all variable, node-specific application state. */
plugin_fileserver_t pfs;

void _plugin_init() {
	snri_log(LOG_DEBUG, "registering\n");

	/* Register the globals here so DVN can track them per-node */
	snri_register_globals(1, sizeof(plugin_fileserver_t), &pfs);
}

void _plugin_uninit() {}

void _plugin_instantiate(gint argc, gchar * argv[]) {
	snri_log(LOG_DEBUG, "parsing args\n");
	if(argc != 2) {
		snri_log(LOG_WARN, "wrong number of args. expected 2.\n");
		snri_log(LOG_MSG, "USAGE: listen_port path/to/docroot\n");
		return;
	}

	in_addr_t listen_addr = INADDR_ANY;
	in_port_t listen_port = (in_port_t) atoi(argv[0]);
	gchar* docroot = argv[1];

	snri_log(LOG_DEBUG, "starting fileserver on port %u\n", listen_port);
	enum fileserver_code res = fileserver_start(&pfs.fs, htonl(listen_addr), htons(listen_port), docroot, 100);

	if(res == FS_SUCCESS) {
		snri_log(LOG_MSG, "fileserver running on at %s:%u\n", inet_ntoa((struct in_addr){listen_addr}),listen_port);
	} else {
		snri_log(LOG_CRIT, "fileserver error, not started!\n");
	}
}

void _plugin_destroy() {
	snri_log(LOG_MSG, "fileserver stats: %lu bytes in, %lu bytes out, %lu replies\n",
			pfs.fs.bytes_received, pfs.fs.bytes_sent, pfs.fs.replies_sent);
	snri_log(LOG_INFO, "shutting down fileserver\n");
	fileserver_shutdown(&pfs.fs);
}

static void plugin_fileserver_activate(gint sockd) {
	enum fileserver_code result = fileserver_activate(&pfs.fs, sockd);
	snri_log(LOG_INFO, "fileserver activation result: %s (%lu bytes in, %lu bytes out, %lu replies)\n",
			fileserver_codetoa(result), pfs.fs.bytes_received, pfs.fs.bytes_sent, pfs.fs.replies_sent);
}

void _plugin_socket_readable(gint sockd){
	plugin_fileserver_activate(sockd);
}

void _plugin_socket_writable(gint sockd){
	plugin_fileserver_activate(sockd);
}
