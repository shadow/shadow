/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shd-scallion.h"
#include <openssl/rand.h>
#include <event2/thread.h>

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Scallion scallion;
/* needed because we dont link tor_main.c */
const char tor_git_revision[] = "";

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
			scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
	}

	return addr;
}

static void _scallion_new(gint argc, gchar* argv[]) {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_new called");

	gchar* usage = "Scallion USAGE: (\"dirauth\"|\"bridgeauth\"|\"relay\"|\"exitrelay\"|\"bridge\"|\"client\"|\"bridgeclient\") consensusWeightKiB ...\n";

	if(argc < 3) {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}
	
	/* parse our arguments, program name is argv[0] */
	gchar* type = argv[1];
	gint weight = atoi(argv[2]);

	enum vtor_nodetype ntype;

	if(g_ascii_strncasecmp(type, "dirauth", strlen("dirauth")) == 0) {
		ntype = VTOR_DIRAUTH;
	} else if(g_ascii_strncasecmp(type, "hsauth", strlen("hsauth")) == 0) {
		ntype = VTOR_HSAUTH;
	} else if(g_ascii_strncasecmp(type, "bridgeauth", strlen("bridgeauth")) == 0) {
		ntype = VTOR_BRIDGEAUTH;
	} else if(g_ascii_strncasecmp(type, "relay", strlen("relay")) == 0) {
		ntype = VTOR_RELAY;
	} else if(g_ascii_strncasecmp(type, "exitrelay", strlen("exitrelay")) == 0) {
		ntype = VTOR_EXITRELAY;
	} else if(g_ascii_strncasecmp(type, "bridge", strlen("bridge")) == 0) {
		ntype = VTOR_BRIDGE;
	} else if(g_ascii_strncasecmp(type, "client", strlen("client")) == 0) {
		ntype = VTOR_CLIENT;
	} else if(g_ascii_strncasecmp(type, "bridgeclient", strlen("bridgeclient")) == 0) {
		ntype = VTOR_BRIDGECLIENT;
	} else {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "Unrecognized relay type: %s", usage);
		return;
	}

	/* get the hostname, IP, and IP string */
	if(gethostname(scallion.hostname, 128) < 0) {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "error getting hostname");
		return;
	}
	scallion.ip = _scallion_HostnameCallback(scallion.hostname);
	inet_ntop(AF_INET, &scallion.ip, scallion.ipstring, sizeof(scallion.ipstring));

	scallion.stor = scalliontor_new(scallion.shadowlibFuncs, scallion.hostname, ntype, weight, argc-3, &argv[3]);
}

static void _scallion_free() {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_free called");
	scalliontor_free(scallion.stor);
}

static void _scallion_notify() {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "_scallion_notify called");
	scalliontor_notify(scallion.stor);
}

/* called immediately after the plugin is loaded. shadow loads plugins once for
 * each worker thread. the GModule* is needed as a handle for g_module_symbol()
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
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	scallion.shadowlibFuncs = shadowlibFuncs;

	/* tell shadow which functions it should call to manage nodes */
	shadowlibFuncs->registerPlugin(&_scallion_new, &_scallion_free, &_scallion_notify);

	shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "finished registering scallion plug-in state");

	/* setup openssl locks */

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

	shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "finished initializing crypto thread state");
#else
    /* no thread support */
	shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild openssl with threading support. expect segfaults.");
#endif

	/* setup libevent locks */

#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
	if(evthread_use_pthreads()) {
		shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "error in evthread_use_pthreads()");
	}
	shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "finished initializing event thread state evthread_use_pthreads()");
#else
	shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild libevent with threading support, or link with event_pthread. expect segfaults.");
#endif
}

static void _scallion_cleanupOpenSSL() {
	EVP_cleanup();
	ERR_remove_state(0);
	ERR_free_strings();

	#ifndef DISABLE_ENGINES
	  ENGINE_cleanup();
	#endif

	CONF_modules_unload(1);
	CRYPTO_cleanup_all_ex_data();
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread.
 */
void g_module_unload(GModule *module) {
	/* TODO check if the following is safe to call once per thread */
	//_scallion_cleanupOpenSSL();
	memset(&scallion, 0, sizeof(Scallion));
}
