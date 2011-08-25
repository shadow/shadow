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

#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

/* This plugin implements the module_interface */
#include "shd-plugin-interface.h"
#include "shd-plugin.h"

/* this plugin runs a filegetter service */
#include "shd-service-filegetter.h"
#include "shd-plugin-filegetter-util.h"

/* my global structure to hold all variable, node-specific application state. */
service_filegetter_t sfg;

void _plugin_init() {
	/* Register the globals here so shadow can track them per-node */
	snri_register_globals(1, sizeof(service_filegetter_t), &sfg);
}

void _plugin_uninit() {}

void _plugin_instantiate(int argc, char * argv[]) {
	const char* usage = "USAGE:\n\t\'single\' http_host http_port (socks_host|\'none\') socks_port num_downloads filepath\n\t--or--\n\t\'double\' http_host http_port (socks_host|\'none\') socks_port filepath1 filepath2 (filepath3|\'none\') pausetime_seconds\n\t--or--\n\t\'multi\' server_specification_filepath (socks_host|\'none\') socks_port (thinktimes_cdf_filepath|\'none\') (runtime_seconds|-1)\n";
	int mode = 0;

	if(argc < 1) {
		snri_log(LOG_WARN, usage);
		return;
	}

	if(strncmp(argv[0], "single", 6) == 0) {
		mode = 1;
	} else if(strncmp(argv[0], "double", 6) == 0) {
		mode = 2;
	} else if(strncmp(argv[0], "multi", 5) == 0) {
		mode = 3;
	} else {
		snri_log(LOG_WARN, usage);
		return;
	}

	int sockd = 0;

	if(mode == 1) {
		service_filegetter_single_args_t args;

		args.http_server.host = argv[1];
		args.http_server.port = argv[2];
		args.socks_proxy.host = argv[3];
		args.socks_proxy.port = argv[4];
		args.num_downloads = argv[5];
		args.filepath = argv[6];

		args.log_cb = &plugin_filegetter_util_log_callback;
		args.hostbyname_cb = &plugin_filegetter_util_hostbyname_callback;

		service_filegetter_start_single(&sfg, &args, &sockd);
	} else if(mode == 2){
		service_filegetter_double_args_t args;

		args.http_server.host = argv[1];
		args.http_server.port = argv[2];
		args.socks_proxy.host = argv[3];
		args.socks_proxy.port = argv[4];
		args.filepath1 = argv[5];
		args.filepath2 = argv[6];
		args.filepath3 = argv[7];
		args.pausetime_seconds = argv[8];

		args.log_cb = &plugin_filegetter_util_log_callback;
		args.hostbyname_cb = &plugin_filegetter_util_hostbyname_callback;
		args.sleep_cb = &plugin_filegetter_util_sleep_callback;

		service_filegetter_start_double(&sfg, &args, &sockd);
	} else {
		service_filegetter_multi_args_t args;

		args.server_specification_filepath = argv[1];
		args.socks_proxy.host = argv[2];
		args.socks_proxy.port = argv[3];
		args.thinktimes_cdf_filepath = argv[4];
		args.runtime_seconds = argv[5];

		if(strncmp(args.thinktimes_cdf_filepath, "none", 4) == 0) {
			args.thinktimes_cdf_filepath = NULL;
		}

		args.log_cb = &plugin_filegetter_util_log_callback;
		args.hostbyname_cb = &plugin_filegetter_util_hostbyname_callback;
		args.sleep_cb = &plugin_filegetter_util_sleep_callback;

		service_filegetter_start_multi(&sfg, &args, &sockd);
	}

	service_filegetter_activate(&sfg, sockd);
}

void _plugin_destroy() {
	service_filegetter_stop(&sfg);
}

void _plugin_socket_readable(int sockd){
	service_filegetter_activate(&sfg, sockd);
}

void _plugin_socket_writable(int sockd){
	service_filegetter_activate(&sfg, sockd);
}
