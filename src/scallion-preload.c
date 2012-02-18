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


#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdint.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include <preload.h>

#define TOR_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in DVN since we expect the locations of the
 * functions to be the same for all nodes.
 */

typedef int (*tor_open_socket_fp)(int, int, int);
static tor_open_socket_fp _vtor_tor_open_socket_fp = NULL;
int tor_open_socket(int domain, int type, int protocol) {
	tor_open_socket_fp* fp_ptr = &_vtor_tor_open_socket_fp;
	char* f_name = TOR_LIB_PREFIX "tor_open_socket";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(domain, type, protocol);
}

typedef int (*tor_gettimeofday_fp)(struct timeval *);
static tor_gettimeofday_fp _vtor_tor_gettimeofday_fp = NULL;
void tor_gettimeofday(struct timeval *timeval) {
	tor_gettimeofday_fp* fp_ptr = &_vtor_tor_gettimeofday_fp;
	char* f_name = TOR_LIB_PREFIX "tor_gettimeofday";

	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(timeval);
}

typedef void (*logv_fp)();
static logv_fp _logv_fp = NULL;
void logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap) {
	logv_fp* fp_ptr = &_logv_fp;
	char* f_name = TOR_LIB_PREFIX "logv";

	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(severity, domain, funcname, format, ap);
}

typedef int (*spawn_func_fp)();
static spawn_func_fp _spawn_func_fp = NULL;
int spawn_func(void (*func)(void *), void *data) {
	spawn_func_fp* fp_ptr = &_spawn_func_fp;
	char* f_name = TOR_LIB_PREFIX "spawn_func";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(func, data);
}

typedef int (*rep_hist_bandwidth_assess_fp)();
static rep_hist_bandwidth_assess_fp _rep_hist_bandwidth_assess_fp = NULL;
int rep_hist_bandwidth_assess(void) {
	rep_hist_bandwidth_assess_fp* fp_ptr = &_rep_hist_bandwidth_assess_fp;
	char* f_name = TOR_LIB_PREFIX "rep_hist_bandwidth_assess";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)();
}

typedef int (*router_get_advertised_bandwidth_capped_fp)(void*);
static router_get_advertised_bandwidth_capped_fp _router_get_advertised_bandwidth_capped_fp = NULL;
uint32_t router_get_advertised_bandwidth_capped(void *router) {
	router_get_advertised_bandwidth_capped_fp* fp_ptr = &_router_get_advertised_bandwidth_capped_fp;
	char* f_name = TOR_LIB_PREFIX "router_get_advertised_bandwidth_capped";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(router);
}

typedef int (*event_base_loopexit_fp)();
static event_base_loopexit_fp _event_base_loopexit_fp = NULL;
int event_base_loopexit(struct event_base * base, const struct timeval * t) {
	event_base_loopexit_fp* fp_ptr = &_event_base_loopexit_fp;
	char* f_name = TOR_LIB_PREFIX "event_base_loopexit";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base, t);
}
