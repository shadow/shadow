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

#include <string.h>
#include <arpa/inet.h>
#include "scallion-plugin.h"

PluginFunctionTable scallion_pluginFunctions = {
	&scallion_new, &scallion_free, &scallion_readable, &scallion_writable,
};

typedef struct scallion_launch_client_s {
	uint8_t is_single;
	void* service_filegetter_args;
} scallion_launch_client_t, *scallion_launch_client_tp;

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Scallion scallion;

void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	/* clear our memory before initializing */
	memset(&scallion, 0, sizeof(Scallion));

	/* save the shadow functions we will use */
	scallion.shadowlibFuncs = shadowlibFuncs;

	/* register all of our state with shadow */
	scallion_register_globals(&scallion_pluginFunctions, &scallion);

	shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "finished registering scallion plug-in state");
}

static void _scallion_logCallback(enum service_filegetter_loglevel level, const gchar* message) {
	if(level == SFG_CRITICAL) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", message);
	} else if(level == SFG_WARNING) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "%s", message);
	} else if(level == SFG_NOTICE) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s", message);
	} else if(level == SFG_INFO) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "%s", message);
	} else if(level == SFG_DEBUG) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s", message);
	} else {
		/* we dont care */
	}
}

static in_addr_t _scallion_HostnameCallback(const gchar* hostname) {
	in_addr_t addr = 0;

	/* get the address in network order */
	if(g_strncasecmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(g_strncasecmp(hostname, "localhost", 9) == 0) {
		addr = htonl(INADDR_LOOPBACK);
	} else {
		addr = scallion.shadowlibFuncs->resolveHostname((gchar*) hostname);
	}

	return addr;
}

static void _scallion_wakeupCallback(gpointer data) {
	service_filegetter_activate((service_filegetter_tp) data, 0);
}

/* called from inner filegetter code when it wants to sleep for some seconds */
static void _scallion_sleepCallback(gpointer sfg, guint seconds) {
	/* schedule a callback from shadow to wakeup the filegetter */
	scallion.shadowlibFuncs->createCallback(&_scallion_wakeupCallback, sfg, seconds*1000);
}

static void scallion_start_socks_client(void* arg) {
	scallion_launch_client_tp launch = arg;

	/* launch our filegetter */
	if(launch != NULL) {
		int sockd = 0;

		if(launch->is_single == 1) {
			service_filegetter_single_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_single(&scallion.sfg, args, &sockd);

			free(args->http_server.host);
			free(args->http_server.port);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			free(args->num_downloads);
			free(args->filepath);
			free(args);
		} else if(launch->is_single == 2) {
			service_filegetter_double_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_double(&scallion.sfg, args, &sockd);

			free(args->http_server.host);
			free(args->http_server.port);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			free(args->filepath1);
			free(args->filepath2);
			free(args->filepath3);
			free(args->pausetime_seconds);
			free(args);
		} else {
			service_filegetter_multi_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_multi(&scallion.sfg, args, &sockd);

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

		service_filegetter_activate(&scallion.sfg, sockd);
	}
}

void scallion_new(int argc, char* argv[]) {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_new called");

	gchar* usage = "Scallion USAGE: (\"dirauth\"|\"relay\"|\"exitrelay\"|\"client\") torrc_path datadir_base_path geoip_path [client_args for shd-plugin-filegetter...]\n";

	const int argcc = argc;
	if(argcc < 1) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}

	/* parse our arguments */
	gchar* tortype = argv[0];
	gchar* bandwidth = argv[1];
	gchar* torrc_path = argv[2];
	gchar* datadir_base_path = argv[3];
	gchar* geoip_path = argv[4];

	enum vtor_nodetype ntype;

	if(g_strncasecmp(tortype, "dirauth", strlen("dirauth")) == 0) {
		ntype = VTOR_DIRAUTH;
	} else if(g_strncasecmp(tortype, "relay", strlen("relay")) == 0) {
		ntype = VTOR_RELAY;
	} else if(g_strncasecmp(tortype, "exitrelay", strlen("exitrelay")) == 0) {
		ntype = VTOR_EXITRELAY;
	} else if(g_strncasecmp(tortype, "client", strlen("client")) == 0) {
		ntype = VTOR_CLIENT;
	} else {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}

	if(ntype != VTOR_CLIENT && argc != 5) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}

	/* get IP address through SNRI */
	scallion.ip = scallion.shadowlibFuncs->getIP();

	/* also save IP as string */
	inet_ntop(AF_INET, &scallion.ip, scallion.ipstring, sizeof(scallion.ipstring));

	/* get the hostname */
	if(!scallion.shadowlibFuncs->getHostname(scallion.hostname, 128)) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "error getting hostname");
		return;
	}

	/* setup actual data directory for this node */
	int size = snprintf(NULL, 0, "%s/%s", datadir_base_path, scallion.hostname) + 1;
	char datadir_path[size];
	sprintf(datadir_path, "%s/%s", datadir_base_path, scallion.hostname);

	/* init deps as needed */
	scallion.shadowlibFuncs->setLoopExit(&vtor_loopexit_cb);

	scallion.vtor.shadowlibFuncs = scallion.shadowlibFuncs;
	vtor_instantiate(&scallion.vtor, scallion.hostname, ntype, bandwidth, torrc_path, datadir_path, geoip_path);

	scallion.sfg.fg.sockd = 0;

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

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

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

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;

			launch->is_single = 1;
			launch->service_filegetter_args = args;
		} else if(strncmp(argv[5], "double", 6) == 0 && argc == 14) {
			service_filegetter_double_args_tp args = malloc(sizeof(service_filegetter_double_args_t));

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
			args->filepath1 = malloc(s);
			snprintf(args->filepath1, s, argv[10]);

			s = strnlen(argv[11], 128)+1;
			args->filepath2 = malloc(s);
			snprintf(args->filepath2, s, argv[11]);

			s = strnlen(argv[12], 128)+1;
			args->filepath3 = malloc(s);
			snprintf(args->filepath3, s, argv[12]);

			s = strnlen(argv[13], 128)+1;
			args->pausetime_seconds = malloc(s);
			snprintf(args->pausetime_seconds, s, argv[13]);

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

			launch->is_single = 2;
			launch->service_filegetter_args = args;
		} else {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
			return;
		}

		scallion.shadowlibFuncs->createCallback(&scallion_start_socks_client, launch, 180000);
	}

}

void scallion_free() {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_free called");

	vtor_destroy();
}

void scallion_readable(int socketDesriptor) {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_readable called");

	if(socketDesriptor == scallion.sfg.fg.sockd) {
		service_filegetter_activate(&scallion.sfg, socketDesriptor);
	} else {
		vtor_socket_readable(&scallion.vtor, socketDesriptor);
	}
}

void scallion_writable(int socketDesriptor) {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_writable called");

	if(socketDesriptor == scallion.sfg.fg.sockd) {
		service_filegetter_activate(&scallion.sfg, socketDesriptor);
	} else {
		vtor_socket_writable(&scallion.vtor, socketDesriptor);
	}
}
