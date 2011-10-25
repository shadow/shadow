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

#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdint.h>

#include <preload.h>

#include "vtor.h"
//#include "tor_includes.h"

#define TOR_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in DVN since we expect the locations of the
 * functions to be the same for all nodes.
 */
typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef void (*logv_fp)();
typedef int (*spawn_func_fp)();
typedef int (*rep_hist_bandwidth_assess_fp)();
//typedef uint32_t (*router_get_advertised_bandwidth_fp)(void *);
//typedef uint32_t (*router_get_advertised_bandwidth_capped_fp)(void *);
//typedef int (*_evdns_nameserver_add_impl_fp)(const struct sockaddr *, socklen_t);

/* save pointers to dvn vsystem functions */
static tor_open_socket_fp _vtor_tor_open_socket_fp = NULL;
static tor_gettimeofday_fp _vtor_tor_gettimeofday_fp = NULL;
static logv_fp _logv_fp = NULL;
static spawn_func_fp _spawn_func_fp = NULL;
static rep_hist_bandwidth_assess_fp _rep_hist_bandwidth_assess_fp = NULL;
//static router_get_advertised_bandwidth_fp _router_get_advertised_bandwidth_fp = NULL;
//static router_get_advertised_bandwidth_capped_fp _router_get_advertised_bandwidth_capped_fp = NULL;
//static _evdns_nameserver_add_impl_fp _vtor__evdns_nameserver_add_impl_fp = NULL;

int tor_open_socket(int domain, int type, int protocol) {
	tor_open_socket_fp* fp_ptr = &_vtor_tor_open_socket_fp;
	char* f_name = TOR_LIB_PREFIX "tor_open_socket";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
	tor_gettimeofday_fp* fp_ptr = &_vtor_tor_gettimeofday_fp;
	char* f_name = TOR_LIB_PREFIX "tor_gettimeofday";

	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(timeval);
}

void logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap) {
	logv_fp* fp_ptr = &_logv_fp;
	char* f_name = TOR_LIB_PREFIX "logv";

	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(severity, domain, funcname, format, ap);
}

int spawn_func(void (*func)(void *), void *data) {
	spawn_func_fp* fp_ptr = &_spawn_func_fp;
	char* f_name = TOR_LIB_PREFIX "spawn_func";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(func, data);
}

int rep_hist_bandwidth_assess(void) {
	rep_hist_bandwidth_assess_fp* fp_ptr = &_rep_hist_bandwidth_assess_fp;
	char* f_name = TOR_LIB_PREFIX "rep_hist_bandwidth_assess";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)();
}

//uint32_t router_get_advertised_bandwidth(void *router) { /* type is (routerinfo_t *) */
//	router_get_advertised_bandwidth_fp* fp_ptr = &_router_get_advertised_bandwidth_fp;
//	char* f_name = TOR_LIB_PREFIX "router_get_advertised_bandwidth";
//
//	PRELOAD_LOOKUP(fp_ptr, f_name, 5120000);
//	return (*fp_ptr)(router);
//}
//
//uint32_t router_get_advertised_bandwidth_capped(void *router) { /* type is (routerinfo_t *) */
//	router_get_advertised_bandwidth_capped_fp* fp_ptr = &_router_get_advertised_bandwidth_capped_fp;
//	char* f_name = TOR_LIB_PREFIX "router_get_advertised_bandwidth_capped";
//
//	PRELOAD_LOOKUP(fp_ptr, f_name, 5120000);
//	return (*fp_ptr)(router);
//}

//int _evdns_nameserver_add_impl(const struct sockaddr *address, socklen_t addrlen) {
//	_evdns_nameserver_add_impl_fp* fp_ptr = &_vtor__evdns_nameserver_add_impl_fp;
//	char* f_name = TOR_LIB_PREFIX "_evdns_nameserver_add_impl";
//
//	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
//	return (*fp_ptr)(address, addrlen);
//}
