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
#include <openssl/rand.h>

typedef struct scallion_launch_client_s {
	uint8_t is_single;
	void* service_filegetter_args;
} scallion_launch_client_t, *scallion_launch_client_tp;

typedef struct scallion_launch_torrent_s {
	uint8_t isAuthority;
	void* torrent_args;
} scallion_launch_torrent_t, *scallion_launch_torrent_tp;

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Scallion scallion;

static void _scallion_sfgLogCallback(enum service_filegetter_loglevel level, const gchar* message) {
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

static void _scallion_torrentLogCallback(enum torrentService_loglevel level, const gchar* message) {
	if(level == TSVC_CRITICAL) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "%s", message);
	} else if(level == TSVC_WARNING) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "%s", message);
	} else if(level == TSVC_NOTICE) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s", message);
	} else if(level == TSVC_INFO) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "%s", message);
	} else if(level == TSVC_DEBUG) {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "%s", message);
	} else {
		/* we dont care */
	}
}

static in_addr_t _scallion_HostnameCallback(const gchar* hostname) {
	in_addr_t addr = 0;

	/* get the address in network order */
	if(g_ascii_strncasecmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(g_ascii_strncasecmp(hostname, "localhost", 9) == 0) {
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

		if(launch->is_single) {
			service_filegetter_single_args_tp args = launch->service_filegetter_args;

			service_filegetter_start_single(&scallion.sfg, args, scallion.sfgEpoll, &sockd);

			free(args->http_server.host);
			free(args->http_server.port);
			free(args->socks_proxy.host);
			free(args->socks_proxy.port);
			free(args->num_downloads);
			free(args->filepath);
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
			if(args->num_downloads != NULL) {
				free(args->num_downloads);
			}
			free(args);
		}
		free(launch);

		service_filegetter_activate(&scallion.sfg, sockd);
	}
}

static void scallion_start_torrent(void* arg) {
	scallion_launch_torrent_tp launch = arg;

	/* launch our filegetter */
	if(launch != NULL) {
		scallion.tsvcServerEpoll = epoll_create(1);
		scallion.tsvcClientEpoll = epoll_create(1);
		int sockd = 0;

		TorrentService_NodeArgs *args = launch->torrent_args;

		torrentService_startNode(&(scallion.tsvc), args, scallion.tsvcServerEpoll, scallion.tsvcClientEpoll, &sockd);

		free(args->authorityHostname);
		free(args->authorityPort);
		free(args->socksHostname);
		free(args->socksPort);
		free(args->serverPort);
		free(args->fileSize);
		if(args->downBlockSize) free(args->downBlockSize);
		if(args->upBlockSize) free(args->upBlockSize);
		free(args);
		free(launch);

		torrentService_activate(&(scallion.tsvc), scallion.tsvc.client->authSockd, EPOLLOUT, scallion.tsvcClientEpoll);
	}
}

static void scallion_start_browser(void* arg) {
	g_assert(arg);
	browser_args_tp args = arg;
	scallion.browserEpoll = epoll_create(1);
	scallion.browser.shadowlib = scallion.shadowlibFuncs;
	gint sockfd = browser_launch(&scallion.browser, args, scallion.browserEpoll);
	
	free(args->http_server.host);
	free(args->http_server.port);
	free(args->socks_proxy.host);
	free(args->socks_proxy.port);
	free(args->max_concurrent_downloads);
	free(args->document_path);
	free(args);

	browser_activate(&scallion.browser, sockfd);
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

	gchar* usage = "Scallion USAGE: (\"dirauth\"|\"relay\"|\"exitrelay\"|\"client\"|\"torrent\"|\"browser\") consensusbandwidth readbandwidthrate writebandwidthrate torrc_path datadir_base_path geoip_path [args for client, torrent or browser node...]\n";

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

	if(g_ascii_strncasecmp(tortype, "dirauth", strlen("dirauth")) == 0) {
		ntype = VTOR_DIRAUTH;
	} else if(g_ascii_strncasecmp(tortype, "relay", strlen("relay")) == 0) {
		ntype = VTOR_RELAY;
	} else if(g_ascii_strncasecmp(tortype, "exitrelay", strlen("exitrelay")) == 0) {
		ntype = VTOR_EXITRELAY;
	} else if(g_ascii_strncasecmp(tortype, "client", strlen("client")) == 0) {
		ntype = VTOR_CLIENT;
	} else if(g_ascii_strncasecmp(tortype, "torrent", strlen("torrent")) == 0) {
		ntype = VTOR_TORRENT;
	} else if(g_ascii_strncasecmp(tortype, "browser", strlen("browser")) == 0) {
		ntype = VTOR_BROWSER;
	} else {
		scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Unrecognized torrent type: %s", usage);
		return;
	}

	if(ntype != VTOR_CLIENT && ntype != VTOR_TORRENT && ntype != VTOR_BROWSER && argc != 7) {
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
	int size = g_snprintf(NULL, 0, "%s/%s", datadir_base_path, scallion.hostname) + 1;
	char datadir_path[size];
	g_sprintf(datadir_path, "%s/%s", datadir_base_path, scallion.hostname);

	scallion.stor = scalliontor_new(scallion.shadowlibFuncs, scallion.hostname, ntype, bandwidth, bwrate, bwburst, torrc_path, datadir_path, geoip_path);

	scallion.sfg.fg.sockd = 0;

	if(ntype == VTOR_CLIENT) {
		gchar** argvoffset = argv + 7;

		/* get filegetter client args */
		scallion_launch_client_tp launch = malloc(sizeof(scallion_launch_client_t));

		gchar* filetransferMode = argvoffset[0];
		if(g_strncasecmp(filetransferMode, "client", 6) != 0) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
			return;
		}

		gchar* fileClientMode = argvoffset[1];

		if(g_strncasecmp(fileClientMode, "multi", 5) == 0 && (argc == 14 || argc == 15)) {
			service_filegetter_multi_args_tp args = g_new0(service_filegetter_multi_args_t, 1);

			size_t s;

			s = strnlen(argvoffset[2], 128)+1;
			args->server_specification_filepath = _scallion_getHomePath(argvoffset[2]);

			s = strnlen(argvoffset[3], 128)+1;
			args->socks_proxy.host = malloc(s);
			g_snprintf(args->socks_proxy.host, s, "%s", argvoffset[3]);

			s = strnlen(argvoffset[4], 128)+1;
			args->socks_proxy.port = malloc(s);
			g_snprintf(args->socks_proxy.port, s, "%s", argvoffset[4]);

			args->thinktimes_cdf_filepath = _scallion_getHomePath(argvoffset[5]);

			s = strnlen(argvoffset[6], 128)+1;
			args->runtime_seconds = malloc(s);
			g_snprintf(args->runtime_seconds, s, "%s", argvoffset[6]);

			if(argc > 14) {
				s = strnlen(argvoffset[7], 128)+1;
				args->num_downloads = malloc(s);
				g_snprintf(args->num_downloads, s, "%s", argvoffset[7]);
			}

			if(g_strncasecmp(args->thinktimes_cdf_filepath, "none", 4) == 0) {
				free(args->thinktimes_cdf_filepath);
				args->thinktimes_cdf_filepath = NULL;
			}

			args->log_cb = &_scallion_sfgLogCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

			launch->is_single = 0;
			launch->service_filegetter_args = args;
		} else if(g_strncasecmp(fileClientMode, "single", 6) == 0 && argc == 15) {
			service_filegetter_single_args_tp args = malloc(sizeof(service_filegetter_single_args_t));

			size_t s;

			s = strnlen(argvoffset[2], 128)+1;
			args->http_server.host = malloc(s);
			g_snprintf(args->http_server.host, s, "%s", argvoffset[2]);

			s = strnlen(argvoffset[3], 128)+1;
			args->http_server.port = malloc(s);
			g_snprintf(args->http_server.port, s, "%s", argvoffset[3]);

			s = strnlen(argvoffset[4], 128)+1;
			args->socks_proxy.host = malloc(s);
			g_snprintf(args->socks_proxy.host, s, "%s", argvoffset[4]);

			s = strnlen(argvoffset[5], 128)+1;
			args->socks_proxy.port = malloc(s);
			g_snprintf(args->socks_proxy.port, s, "%s", argvoffset[5]);

			s = strnlen(argvoffset[6], 128)+1;
			args->num_downloads = malloc(s);
			g_snprintf(args->num_downloads, s, "%s", argvoffset[6]);

			args->filepath = _scallion_getHomePath(argvoffset[7]);

			args->log_cb = &_scallion_sfgLogCallback;
			args->hostbyname_cb = &_scallion_HostnameCallback;
			args->sleep_cb = &_scallion_sleepCallback;

			launch->is_single = 1;
			launch->service_filegetter_args = args;
		} else {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
			return;
		}

		scallion.shadowlibFuncs->createCallback(&scallion_start_socks_client, launch, 600000);
	} else if(ntype == VTOR_TORRENT) {
		gchar** argvoffset = argv + 9;

		scallion_launch_torrent_tp launch = malloc(sizeof(scallion_launch_torrent_t));

		TorrentService_NodeArgs *args = malloc(sizeof(TorrentService_NodeArgs));
		size_t s;

		s = strnlen(argvoffset[0], 128)+1;
		args->authorityHostname = malloc(s);
		g_snprintf(args->authorityHostname, s, "%s", argvoffset[0]);

		s = strnlen(argvoffset[1], 128)+1;
		args->authorityPort = malloc(s);
		g_snprintf(args->authorityPort, s, "%s", argvoffset[1]);

		s = strnlen(argvoffset[2], 128)+1;
		args->socksHostname = malloc(s);
		g_snprintf(args->socksHostname, s, "%s", argvoffset[2]);

		s = strnlen(argvoffset[3], 128)+1;
		args->socksPort = malloc(s);
		g_snprintf(args->socksPort, s, "%s", argvoffset[3]);

		s = strnlen(argvoffset[4], 128)+1;
		args->serverPort = malloc(s);
		g_snprintf(args->serverPort, s, "%s", argvoffset[4]);

		s = strnlen(argvoffset[5], 128)+1;
		args->fileSize = malloc(s);
		g_snprintf(args->fileSize, s, "%s", argvoffset[5]);

		args->downBlockSize = NULL;
		args->upBlockSize = NULL;

		if(argc == 17) {
			s = strnlen(argvoffset[6], 128)+1;
			args->downBlockSize = malloc(s);
			g_snprintf(args->downBlockSize, s, "%s", argvoffset[6]);

			s = strnlen(argvoffset[7], 128)+1;
			args->upBlockSize = malloc(s);
			g_snprintf(args->upBlockSize, s, "%s", argvoffset[7]);
		}

		args->log_cb = &_scallion_torrentLogCallback;
		args->hostbyname_cb = &_scallion_HostnameCallback;

		launch->isAuthority = 0;
		launch->torrent_args = args;

		scallion.shadowlibFuncs->createCallback(&scallion_start_torrent, launch, 600000);
	} else if (ntype == VTOR_BROWSER) {
		gchar** argvoffset = argv + 7;

		browser_args_tp args = g_new0(browser_args_t, 1);
	
		args->http_server.host = g_strdup(argvoffset[0]);
		args->http_server.port = g_strdup(argvoffset[1]);
		args->socks_proxy.host = g_strdup(argvoffset[2]);
		args->socks_proxy.port = g_strdup(argvoffset[3]);
		args->max_concurrent_downloads = g_strdup(argvoffset[4]);
		args->document_path = g_strdup(argvoffset[5]);

		scallion.shadowlibFuncs->createCallback(&scallion_start_browser, args, 600000);
	}

}

static void _scallion_free() {
	scallion.shadowlibFuncs->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_free called");
	
	if (scallion.sfgEpoll) {
		service_filegetter_stop(&scallion.sfg);
	}
	
	if (scallion.browserEpoll) {
		browser_free(&scallion.browser);
	}
	
	if(scallion.tsvcClientEpoll || scallion.tsvcServerEpoll) {
		torrentService_stop(&scallion.tsvc);
	}
	
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

	if(scallion.tsvcClientEpoll) {
		struct epoll_event events[10];
		int nfds = epoll_wait(scallion.tsvcClientEpoll, events, 10, 0);
		if(nfds == -1) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in torrent client epoll_wait");
		} else {
			for(int i = 0; i < nfds; i++) {
				torrentService_activate(&scallion.tsvc, events[i].data.fd, events[i].events, scallion.tsvcClientEpoll);
			}
		}
		if(!scallion.tsvc.client) {
			scallion.tsvcClientEpoll = 0;
		}
	}


	if(scallion.tsvcServerEpoll) {
		struct epoll_event events[10];
		int nfds = epoll_wait(scallion.tsvcServerEpoll, events, 10, 0);
		if(nfds == -1) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in torrent server epoll_wait");
		} else {
			for(int i = 0; i < nfds; i++) {
				torrentService_activate(&scallion.tsvc, events[i].data.fd, events[i].events, scallion.tsvcServerEpoll);
			}
		}
		if(!scallion.tsvc.server) {
			scallion.tsvcServerEpoll = 0;
		}
	}

	if(scallion.browserEpoll) {
		struct epoll_event events[10];
		int nfds = epoll_wait(scallion.browserEpoll, events, 10, 0);
		if(nfds == -1) {
			scallion.shadowlibFuncs->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in browser epoll_wait");
		} else {
			for(int i = 0; i < nfds; i++) {
				browser_activate(&scallion.browser, events[i].data.fd);
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

typedef void (*CRYPTO_lock_func)(int mode,int type, const char *file,int line);
typedef unsigned long (*CRYPTO_id_func)(void);

/* called after g_module_check_init(), after shadow searches for __shadow_plugin_init__ */
void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	scallion.shadowlibFuncs = shadowlibFuncs;

	/* register all of our state with shadow */
	scallion_register_globals(&scallion_pluginFunctions, &scallion);

	shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "finished registering scallion plug-in state");

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#if defined(OPENSSL_THREADS)
	/* thread support enabled */

	/* make sure openssl uses Shadow's random sources and make crypto thread-safe */
	const RAND_METHOD* shadowRandomMethod = NULL;
	CRYPTO_lock_func shadowLockFunc = NULL;
	CRYPTO_id_func shadowIdFunc = NULL;
	int nLocks = CRYPTO_num_locks();

	gboolean success = shadowlibFuncs->cryptoSetup(nLocks, (gpointer*)&shadowLockFunc,
			(gpointer*)&shadowIdFunc, (gconstpointer*)&shadowRandomMethod);
	if(!success) {
		/* ok, lets see if we can get shadow function pointers through LD_PRELOAD */
		shadowRandomMethod = RAND_get_rand_method();
		shadowLockFunc = CRYPTO_get_locking_callback();
		shadowIdFunc = CRYPTO_get_id_callback();
	}

	CRYPTO_set_locking_callback(shadowLockFunc);
	CRYPTO_set_id_callback(shadowIdFunc);
	RAND_set_rand_method(shadowRandomMethod);

	shadowlibFuncs->log(G_LOG_LEVEL_INFO, __FUNCTION__, "finished initializing crypto state");
#else
    /* no thread support */
	shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild openssl with threading support. expect segfaults.");
#endif
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread.
 */
void g_module_unload(GModule *module) {
	memset(&scallion, 0, sizeof(Scallion));
}
