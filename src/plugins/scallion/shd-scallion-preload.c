/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>

#include <glib.h>
#include <gmodule.h>

#include "torlog.h"

#define TOR_LIB_PREFIX "intercept_"

typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef void (*logv_fp)();
typedef int (*spawn_func_fp)();
typedef int (*rep_hist_bandwidth_assess_fp)();
typedef int (*router_get_advertised_bandwidth_capped_fp)(void*);
typedef int (*event_base_loopexit_fp)();
typedef int (*add_callback_log_fp)(const log_severity_list_t *, log_callback);
typedef int (*crypto_global_cleanup_fp)(void);

/* the key used to store each threads version of their searched function library.
 * the use this key to retrieve this library when intercepting functions from tor.
 */
GStaticPrivate scallionWorkerKey;

typedef struct _ScallionPreloadWorker ScallionPreloadWorker;
/* TODO fix func names */
struct _ScallionPreloadWorker {
	GModule* handle;
	tor_open_socket_fp a;
	tor_gettimeofday_fp b;
	logv_fp c;
	spawn_func_fp d;
	rep_hist_bandwidth_assess_fp e;
	router_get_advertised_bandwidth_capped_fp f;
	event_base_loopexit_fp g;
	add_callback_log_fp h;
	crypto_global_cleanup_fp i;
};

/* scallionpreload_init must be called before this so the worker gets created */
static ScallionPreloadWorker* _scallionpreload_getWorker() {
	/* get current thread's private worker object */
	ScallionPreloadWorker* worker = g_static_private_get(&scallionWorkerKey);
	g_assert(worker);
	return worker;
}

static ScallionPreloadWorker* _scallionpreload_newWorker(GModule* handle) {
	ScallionPreloadWorker* worker = g_new0(ScallionPreloadWorker, 1);
	worker->handle = handle;
	return worker;
}

/* here we search and save pointers to the functions we need to call when
 * we intercept tor's functions. this is initialized for each thread, and each
 * thread has pointers to their own functions (each has its own version of the
 * plug-in state). We dont register these function locations, because they are
 * not *node* dependent, only *thread* dependent.
 */

void scallionpreload_init(GModule* handle) {
	ScallionPreloadWorker* worker = _scallionpreload_newWorker(handle);

	/* lookup all our required symbols in this worker's module, asserting success */
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "tor_open_socket", (gpointer*)&(worker->a)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "tor_gettimeofday", (gpointer*)&(worker->b)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "logv", (gpointer*)&(worker->c)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "spawn_func", (gpointer*)&(worker->d)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "rep_hist_bandwidth_assess", (gpointer*)&(worker->e)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "router_get_advertised_bandwidth_capped", (gpointer*)&(worker->f)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "event_base_loopexit", (gpointer*)&(worker->g)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "add_callback_log", (gpointer*)&(worker->h)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "crypto_global_cleanup", (gpointer*)&(worker->i)));

	g_static_private_set(&scallionWorkerKey, worker, g_free);
}

int tor_open_socket(int domain, int type, int protocol) {
	return _scallionpreload_getWorker()->a(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
	_scallionpreload_getWorker()->b(timeval);
}

#ifdef SCALLION_LOGVWITHSUFFIX
void logv(int severity, uint32_t domain, const char *funcname,
	const char *suffix, const char *format, va_list ap) {
	_scallionpreload_getWorker()->c(severity, domain, funcname, suffix, format, ap);
}
#else
void logv(int severity, uint32_t domain, const char *funcname,
    const char *format, va_list ap) {
	_scallionpreload_getWorker()->c(severity, domain, funcname, format, ap);
}
#endif

int spawn_func(void (*func)(void *), void *data) {
	return _scallionpreload_getWorker()->d(func, data);
}

int rep_hist_bandwidth_assess(void) {
	return _scallionpreload_getWorker()->e();
}

uint32_t router_get_advertised_bandwidth_capped(void *router) {
	return _scallionpreload_getWorker()->f(router);
}

/* struct event_base* base */
int event_base_loopexit(gpointer base, const struct timeval * t) {
	return _scallionpreload_getWorker()->g(base, t);
}

int add_callback_log(const log_severity_list_t *severity, log_callback cb) {
    return _scallionpreload_getWorker()->h(severity, cb);
}

int crypto_global_cleanup(void) {
	return _scallionpreload_getWorker()->i();
}
