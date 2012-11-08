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

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>

#include <glib.h>
#include <gmodule.h>

#define TOR_LIB_PREFIX "intercept_"

typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef void (*logv_fp)();
typedef int (*spawn_func_fp)();
typedef int (*rep_hist_bandwidth_assess_fp)();
typedef int (*router_get_advertised_bandwidth_capped_fp)(void*);
typedef int (*event_base_loopexit_fp)();
typedef int (*add_callback_log_fp)(const log_severity_list_t *, log_callback);

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

	g_static_private_set(&scallionWorkerKey, worker, g_free);
}

int tor_open_socket(int domain, int type, int protocol) {
	return _scallionpreload_getWorker()->a(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
	_scallionpreload_getWorker()->b(timeval);
}

void logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap) {
	_scallionpreload_getWorker()->c(severity, domain, funcname, format, ap);
}

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
