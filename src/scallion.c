/**
 * Scallion - plug-in for The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Scallion.
 *
 * Scallion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scallion is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scallion.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <arpa/inet.h>

/* This module implements the module_interface */
#include <shd-plugin-interface.h>

/* This module makes calls to the shadow core through the standard network routing interface */
#include <shd-plugin.h>

#include <shd-plugin-filegetter-util.h>
#include "scallion.h"

/* private timer struct */
typedef struct scallion_timer_s {
	snri_timer_callback_fp cb_function;
	void* cb_arg;
} scallion_timer_t, *scallion_timer_tp;

typedef struct scallion_launch_client_s {
	uint8_t is_single;
	void* service_filegetter_args;
} scallion_launch_client_t, *scallion_launch_client_tp;

/* globals for scallion module */
scallion_t _scallion_global_data;

/* pointer to the scallion globals for the node thats currently executing.
 * NULL if we are not currently in the scallion context. scallion code can
 * access the globals using this pointer.
 *
 * we use a pointer and check against NULL so we dont use a version of the
 * globals that belong to another scallion node instance. this variable is
 * global and should NOT be registered.
 */
scallion_tp scallion = NULL;

static void scallion_enter_context(char* msg) {
//	scallion = &_scallion_global_data;
	snri_log(LOG_DEBUG, "scallion_enter_context from %s\n", msg);
}

static void scallion_exit_context(char* msg) {
//	scallion = NULL;
	snri_log(LOG_DEBUG, "scallion_exit_context from %s\n", msg);
}

//int scallion_create_timer(int milli_delay, snri_timer_callback_fp cb_function, void * cb_arg) {
//	scallion_timer_tp stimer = malloc(sizeof(scallion_timer_t));
//	stimer->cb_arg = cb_arg;
//	stimer->cb_function = cb_function;
//	return snri_timer_create(milli_delay, &_scallion_timeout, stimer);
//}
//
//void _scallion_timeout(int timerid, void* arg) {
//	scallion_enter_context("_scallion_timeout");
//	scallion_timer_tp stimer = arg;
//	if(stimer != NULL) {
//		if(stimer->cb_function != NULL) {
//			(stimer->cb_function)(timerid, stimer->cb_arg);
//		} else {
//			snri_log(LOG_WARN, "_scallion_timeout: ignoring timeout with NULL callback\n");
//		}
//		free(stimer);
//	}
//	scallion_exit_context("_scallion_timeout");
//}

static void scallion_start_socks_client(int timerid, void* arg) {
	scallion_launch_client_tp launch = arg;


	/* launch our filegetter */
	if(launch != NULL) {
		int sockd = 0;

		if(launch->is_single) {
			service_filegetter_single_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_single(&scallion->sfg, args, &sockd);

			free(args->http_server.host);
			free(args->http_server.port);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			free(args->num_downloads);
			free(args->filepath);
			free(args);
		} else {
			service_filegetter_multi_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_multi(&scallion->sfg, args, &sockd);

			free(args->server_specification_filepath);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			if(args->thinktimes_cdf_filepath != NULL) {
				free(args->thinktimes_cdf_filepath);
			}
			free(args->runtime_seconds);
			free(args);
		}
		free(launch);

		service_filegetter_activate(&scallion->sfg, sockd);
	}
}

void _plugin_init() {
	scallion_enter_context("_plugin_init");

	/* clear global memory before registering */
	memset(&_scallion_global_data, 0, sizeof(scallion_t));
	memset(&scallion, 0, sizeof(scallion_tp));

	snri_log(LOG_INFO, "scallion registering variables...\n");
	/* pass in the address of the actual global data */
	scallion_register_globals(&_scallion_global_data, &scallion);
	snri_log(LOG_MSG, "finished registration, scallion initialized!\n");

	scallion_exit_context("_plugin_init");
}

void _plugin_uninit() {}

void _plugin_instantiate(int argc, char* argv[]) {
	scallion_enter_context("_plugin_instantiate");

	const char* usage = "Scallion USAGE: (\"dirauth\"|\"relay\"|\"exitrelay\"|\"client\") torrc_path datadir_base_path geoip_path [client_args for shd-plugin-filegetter...]\n";

	if(argc < 1) {
		snri_log(LOG_CRIT, usage);
		return;
	}

	scallion = &_scallion_global_data;

	/* parse our arguments */
	char* tortype = argv[0];
	char* bandwidth = argv[1];
	char* torrc_path = argv[2];
	char* datadir_base_path = argv[3];
	char* geoip_path = argv[4];

	enum vtor_nodetype ntype;

	if(strncmp(tortype, "dirauth", strlen("dirauth")) == 0) {
		ntype = VTOR_DIRAUTH;
	} else if(strncmp(tortype, "relay", strlen("relay")) == 0) {
		ntype = VTOR_RELAY;
	} else if(strncmp(tortype, "exitrelay", strlen("exitrelay")) == 0) {
		ntype = VTOR_EXITRELAY;
	} else if(strncmp(tortype, "client", strlen("client")) == 0) {
		ntype = VTOR_CLIENT;
	} else {
		snri_log(LOG_CRIT, "arg from DSIM file has unknown tor type '%s' (valid are 'dirauth', 'relay', 'exitrelay', and 'client')\n", tortype);
		snri_log(LOG_CRIT, usage);
		return;
	}

	if(ntype != VTOR_CLIENT && argc != 5) {
		snri_log(LOG_CRIT, usage);
	}

	/* get IP address through SNRI */
	if(snri_getip(&scallion->ip) == SNRICALL_ERROR) {
		snri_log(LOG_CRIT, "Error getting IP address!\n");
		return;
	}

	/* also save IP as string */
	inet_ntop(AF_INET, &scallion->ip, scallion->ipstring, sizeof(scallion->ipstring));

	/* get the hostname */
	if(snri_gethostname(scallion->hostname, 128) == SNRICALL_ERROR) {
		snri_log(LOG_CRIT, "Error getting hostname!\n");
		return;
	}

	/* setup actual data directory for this node */
	int size = snprintf(NULL, 0, "%s/%s", datadir_base_path, scallion->hostname) + 1;
	char datadir_path[size];
	sprintf(datadir_path, "%s/%s", datadir_base_path, scallion->hostname);

	/* init deps as needed */
	snri_set_loopexit_fn(&vtor_loopexit_cb);

	vtor_instantiate(&scallion->vtor, scallion->hostname, ntype, bandwidth, torrc_path, datadir_path, geoip_path);

	scallion->sfg.fg.sockd = 0;

	if(ntype == VTOR_CLIENT) {
		/* get filegetter client args */
		scallion_launch_client_tp launch = malloc(sizeof(scallion_launch_client_t));

		if(strncmp(argv[5], "multi", 5) == 0 && argc == 11) {
			service_filegetter_multi_args_tp args = malloc(sizeof(service_filegetter_multi_args_t));

			size_t s;

			s = strnlen(argv[6], 128)+1;
			args->server_specification_filepath = malloc(s);
			snprintf(args->server_specification_filepath, s, argv[6]);

			s = strnlen(argv[7], 128)+1;
			args->socks_proxy.host = malloc(s);
			snprintf(args->socks_proxy.host, s, argv[7]);

			s = strnlen(argv[8], 128)+1;
			args->socks_proxy.port = malloc(s);
			snprintf(args->socks_proxy.port, s, argv[8]);

			s = strnlen(argv[9], 128)+1;
			args->thinktimes_cdf_filepath = malloc(s);
			snprintf(args->thinktimes_cdf_filepath, s, argv[9]);

			s = strnlen(argv[10], 128)+1;
			args->runtime_seconds = malloc(s);
			snprintf(args->runtime_seconds, s, argv[10]);

			if(strncmp(args->thinktimes_cdf_filepath, "none", 4) == 0) {
				free(args->thinktimes_cdf_filepath);
				args->thinktimes_cdf_filepath = NULL;
			}

			args->log_cb = &plugin_filegetter_util_log_callback;
			args->hostbyname_cb = &plugin_filegetter_util_hostbyname_callback;
			args->sleep_cb = &plugin_filegetter_util_sleep_callback;

			launch->is_single = 0;
			launch->service_filegetter_args = args;
		} else if(strncmp(argv[5], "single", 6) == 0 && argc == 12) {
			service_filegetter_single_args_tp args = malloc(sizeof(service_filegetter_single_args_t));

			size_t s;

			s = strnlen(argv[6], 128)+1;
			args->http_server.host = malloc(s);
			snprintf(args->http_server.host, s, argv[6]);

			s = strnlen(argv[7], 128)+1;
			args->http_server.port = malloc(s);
			snprintf(args->http_server.port, s, argv[7]);

			s = strnlen(argv[8], 128)+1;
			args->socks_proxy.host = malloc(s);
			snprintf(args->socks_proxy.host, s, argv[8]);

			s = strnlen(argv[9], 128)+1;
			args->socks_proxy.port = malloc(s);
			snprintf(args->socks_proxy.port, s, argv[9]);

			s = strnlen(argv[10], 128)+1;
			args->num_downloads = malloc(s);
			snprintf(args->num_downloads, s, argv[10]);

			s = strnlen(argv[11], 128)+1;
			args->filepath = malloc(s);
			snprintf(args->filepath, s, argv[11]);

			args->log_cb = &plugin_filegetter_util_log_callback;
			args->hostbyname_cb = &plugin_filegetter_util_hostbyname_callback;

			launch->is_single = 1;
			launch->service_filegetter_args = args;
		} else {
			snri_log(LOG_CRIT, usage);
			return;
		}

		snri_timer_create(180000, &scallion_start_socks_client, launch);
	}

	scallion_exit_context("_plugin_instantiate");
}

void _plugin_destroy() {
	scallion_enter_context("_plugin_destroy");
	vtor_destroy();
	scallion_exit_context("_plugin_destroy");
}

void _plugin_socket_readable(int sockd){
	scallion_enter_context("_plugin_socket_readable");

	if(sockd == scallion->sfg.fg.sockd) {
		service_filegetter_activate(&scallion->sfg, sockd);
	} else {
		vtor_socket_readable(&scallion->vtor, sockd);
	}

	scallion_exit_context("_plugin_socket_readable");
}
void _plugin_socket_writable(int sockd){
	scallion_enter_context("_plugin_socket_writable");

	if(sockd == scallion->sfg.fg.sockd) {
		service_filegetter_activate(&scallion->sfg, sockd);
	} else {
		vtor_socket_writable(&scallion->vtor, sockd);
	}

	scallion_exit_context("_plugin_socket_writable");
}
