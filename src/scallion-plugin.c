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

#include "scallion.h"

typedef struct scallion_launch_client_s {
	uint8_t is_single;
	void* service_filegetter_args;
} scallion_launch_client_t, *scallion_launch_client_tp;

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Scallion scallion;

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
		struct addrinfo* info;
		int result = getaddrinfo((gchar*) hostname, NULL, NULL, &info);
		if(result != -1 && info != NULL) {
			addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		} else {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
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
		scallion.sfgEpoll = epoll_create(1);
		int sockd = 0;

		if(launch->is_single == 1) {
			service_filegetter_single_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_single(&scallion.sfg, args, scallion.sfgEpoll, &sockd);

			free(args->http_server.host);
			free(args->http_server.port);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			free(args->num_downloads);
			free(args->filepath);
			free(args);
		} else if(launch->is_single == 2) {
			service_filegetter_double_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_double(&scallion.sfg, args, scallion.sfgEpoll, &sockd);

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

			service_filegetter_start_multi(&scallion.sfg, args, scallion.sfgEpoll, &sockd);

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

static gchar* _scallion_getHomePath(gchar* path) {
	GString* sbuffer = g_string_new("");
	if(g_ascii_strncasecmp(path, "~", 1) == 0) {
		/* replace ~ with home directory */
		const gchar* home = g_get_home_dir();
		g_string_append_printf(sbuffer, "%s%s", home, path+1);
	} else {
		g_string_append_printf(sbuffer, "%s", path);
	}
	return g_string_free(sbuffer, FALSE);
}

static void _scallion_new(gint argc, gchar* argv[]) {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_new called");

	gchar* usage = "Scallion USAGE: (\"dirauth\"|\"relay\"|\"exitrelay\"|\"client\") consensusbandwidth readbandwidthrate writebandwidthrate torrc_path datadir_base_path geoip_path [client_args for shd-plugin-filegetter...]\n";

	if(argc < 2) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}

	/* take out program name arg */
	argc--;
	argv = &argv[1];

	/* parse our arguments */
	gchar* tortype = argv[0];
	gchar* bandwidth = argv[1];
	gchar* bwrate = argv[2];
	gchar* bwburst = argv[3];
	gchar* torrc_path = argv[4];
	gchar* datadir_base_path = argv[5];
	gchar* geoip_path = argv[6];

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

	if(ntype != VTOR_CLIENT && argc != 7) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}

	/* get the hostname */
	if(gethostname(scallion.hostname, 128) < 0) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "error getting hostname");
		return;
	}

	/* get IP address through SNRI */
	scallion.ip = _scallion_HostnameCallback(scallion.hostname);

	/* also save IP as string */
	inet_ntop(AF_INET, &scallion.ip, scallion.ipstring, sizeof(scallion.ipstring));

	/* setup actual data directory for this node */
	int size = snprintf(NULL, 0, "%s/%s", datadir_base_path, scallion.hostname) + 1;
	char datadir_path[size];
	sprintf(datadir_path, "%s/%s", datadir_base_path, scallion.hostname);

	scallion.stor = scalliontor_new(scallion.shadowlibFuncs, scallion.hostname, ntype, bandwidth, bwrate, bwburst, torrc_path, datadir_path, geoip_path);

	scallion.sfg.fg.sockd = 0;


	if(ntype == VTOR_CLIENT) {
		gchar** argvoffset = argv + 7;

		/* get filegetter client args */
		scallion_launch_client_tp launch = malloc(sizeof(scallion_launch_client_t));

		gchar* filetransferMode = argvoffset[0];
		if(strncmp(filetransferMode, "client", 6) != 0) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
			return;
		}

		gchar* fileClientMode = argvoffset[1];

		if(strncmp(fileClientMode, "multi", 5) == 0 && argc == 14) {
			service_filegetter_multi_args_tp args = malloc(sizeof(service_filegetter_multi_args_t));

			size_t s;

			s = strnlen(argvoffset[2], 128)+1;
			args->server_specification_filepath = _scallion_getHomePath(argvoffset[2]);

			s = strnlen(argvoffset[3], 128)+1;
			args->socks_proxy.host = malloc(s);
			snprintf(args->socks_proxy.host, s, argvoffset[3]);

			s = strnlen(argvoffset[4], 128)+1;
			args->socks_proxy.port = malloc(s);
			snprintf(args->socks_proxy.port, s, argvoffset[4]);

			args->thinktimes_cdf_filepath = _scallion_getHomePath(argvoffset[5]);

			s = strnlen(argvoffset[6], 128)+1;
			args->runtime_seconds = malloc(s);
			snprintf(args->runtime_seconds, s, argvoffset[6]);

			if(strncmp(args->thinktimes_cdf_filepath, "none", 4) == 0) {
				free(args->thinktimes_cdf_filepath);
				args->thinktimes_cdf_filepath = NULL;
			}

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

			launch->is_single = 0;
			launch->service_filegetter_args = args;
		} else if(strncmp(fileClientMode, "single", 6) == 0 && argc == 15) {
			service_filegetter_single_args_tp args = malloc(sizeof(service_filegetter_single_args_t));

			size_t s;

			s = strnlen(argvoffset[2], 128)+1;
			args->http_server.host = malloc(s);
			snprintf(args->http_server.host, s, argvoffset[2]);

			s = strnlen(argvoffset[3], 128)+1;
			args->http_server.port = malloc(s);
			snprintf(args->http_server.port, s, argvoffset[3]);

			s = strnlen(argvoffset[4], 128)+1;
			args->socks_proxy.host = malloc(s);
			snprintf(args->socks_proxy.host, s, argvoffset[4]);

			s = strnlen(argvoffset[5], 128)+1;
			args->socks_proxy.port = malloc(s);
			snprintf(args->socks_proxy.port, s, argvoffset[5]);

			s = strnlen(argvoffset[6], 128)+1;
			args->num_downloads = malloc(s);
			snprintf(args->num_downloads, s, argvoffset[6]);

			args->filepath = _scallion_getHomePath(argvoffset[7]);

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;

			launch->is_single = 1;
			launch->service_filegetter_args = args;
		} else if(strncmp(fileClientMode, "double", 6) == 0 && argc == 17) {
			service_filegetter_double_args_tp args = malloc(sizeof(service_filegetter_double_args_t));

			size_t s;

			s = strnlen(argvoffset[2], 128)+1;
			args->http_server.host = malloc(s);
			snprintf(args->http_server.host, s, argvoffset[2]);

			s = strnlen(argvoffset[3], 128)+1;
			args->http_server.port = malloc(s);
			snprintf(args->http_server.port, s, argvoffset[3]);

			s = strnlen(argvoffset[4], 128)+1;
			args->socks_proxy.host = malloc(s);
			snprintf(args->socks_proxy.host, s, argvoffset[4]);

			s = strnlen(argvoffset[5], 128)+1;
			args->socks_proxy.port = malloc(s);
			snprintf(args->socks_proxy.port, s, argvoffset[5]);

			args->filepath1 = _scallion_getHomePath(argvoffset[6]);

			args->filepath2 = _scallion_getHomePath(argvoffset[7]);

			args->filepath3 = _scallion_getHomePath(argvoffset[8]);

			s = strnlen(argvoffset[9], 128)+1;
			args->pausetime_seconds = malloc(s);
			snprintf(args->pausetime_seconds, s, argvoffset[9]);

			args->log_cb = &_scallion_logCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

			launch->is_single = 2;
			launch->service_filegetter_args = args;
		} else {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
			return;
		}

		scallion.shadowlibFuncs->createCallback(&scallion_start_socks_client, launch, 600000);
	}

}

static void _scallion_free() {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_free called");
	scalliontor_free(scallion.stor);
}

static void _scallion_notify() {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "_scallion_notify called");

	/* check clients epoll descriptor for events, and activate each ready socket */
	if(scallion.sfgEpoll) {
		struct epoll_event events[10];
		int nfds = epoll_wait(scallion.sfgEpoll, events, 10, 0);
		if(nfds == -1) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
		} else {
			/* finally, activate client for every socket thats ready */
			for(int i = 0; i < nfds; i++) {
				service_filegetter_activate(&scallion.sfg, events[i].data.fd);
			}
		}
	}

	scalliontor_notify(scallion.stor);
}

/* these are required to hook into shadow functions */

PluginFunctionTable scallion_pluginFunctions = {
	&_scallion_new, &_scallion_free, &_scallion_notify,
};

/* these are required because shadow loads plugins with
 * in order for the plugin to intercept functions from tor, we need the module
 * handle when doing the dlsym lookup
 */

/* called immediately after the plugin is loaded. shadow loads plugins once for
 * each worker thread. the GModule* can be used as a handle for g_module_symbol()
 * symbol lookups.
 * return NULL for success, or a string describing the error */
const gchar* g_module_check_init(GModule *module) {
	/* clear our memory before initializing */
	memset(&scallion, 0, sizeof(Scallion));

	/* do all the symbol lookups we will need now, and init our thread-specific
	 * library of intercepted functions. */
	scallionpreload_init(module);

	return NULL;
}

/* called after g_module_check_init(), after shadow searches for __shadow_plugin_init__ */
void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	scallion.shadowlibFuncs = shadowlibFuncs;

	/* register all of our state with shadow */
	scallion_register_globals(&scallion_pluginFunctions, &scallion);

	shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "finished registering scallion plug-in state");
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread.
 */
void g_module_unload(GModule *module) {
	memset(&scallion, 0, sizeof(Scallion));
}
