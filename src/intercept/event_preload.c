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

#include <dlfcn.h>

#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "shadow.h"

#define EVENT_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in shadow since we expect the locations of the
 * functions to be the same for all nodes.
 */
typedef struct event_base *(*event_base_new_fp)(void);
typedef struct event_base *(*event_base_new_with_config_fp)(const struct event_config *);
typedef void (*event_base_free_fp)(struct event_base *);
typedef const char *(*event_base_get_method_fp)(const struct event_base *);
typedef void (*event_set_log_callback_fp)(event_log_cb cb);
typedef int (*event_base_loop_fp)(struct event_base *, int);
typedef int (*event_base_loopexit_fp)(struct event_base *, const struct timeval *);
typedef int (*event_assign_fp)(struct event *, struct event_base *, evutil_socket_t, short, event_callback_fn, void *);
typedef struct event *(*event_new_fp)(struct event_base *, evutil_socket_t, short, event_callback_fn, void *);
typedef void (*event_free_fp)(struct event *);
typedef int (*event_add_fp)(struct event *, const struct timeval *);
typedef int (*event_del_fp)(struct event *);
typedef void (*event_active_fp)(struct event *, int, short);
typedef int (*event_pending_fp)(const struct event *, short, struct timeval *);
typedef const char *(*event_get_version_fp)(void);
typedef ev_uint32_t (*event_get_version_number_fp)(void);

/* event2/dns.h */
typedef struct evdns_base * (*evdns_base_new_fp)(struct event_base *event_base, int initialize_nameservers);
typedef const char *(*evdns_err_to_string_fp)(int err);
typedef int (*evdns_base_count_nameservers_fp)(struct evdns_base *base);
typedef int (*evdns_base_clear_nameservers_and_suspend_fp)(struct evdns_base *base);
typedef int (*evdns_base_resume_fp)(struct evdns_base *base);
typedef struct evdns_request *(*evdns_base_resolve_ipv4_fp)(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr);
typedef struct evdns_request *(*evdns_base_resolve_reverse_fp)(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);
typedef struct evdns_request *(*evdns_base_resolve_reverse_ipv6_fp)(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);
typedef int (*evdns_base_set_option_fp)(struct evdns_base *base, const char *option, const char *val);
typedef int (*evdns_base_resolv_conf_parse_fp)(struct evdns_base *base, int flags, const char *const filename);
typedef void (*evdns_base_search_clear_fp)(struct evdns_base *base);
typedef void (*evdns_set_log_fn_fp)(evdns_debug_log_fn_type fn);
typedef void (*evdns_set_random_bytes_fn_fp)(void (*fn)(char *, size_t));
typedef struct evdns_server_port *(*evdns_add_server_port_with_base_fp)(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data);
typedef void (*evdns_close_server_port_fp)(struct evdns_server_port *port);
typedef int (*evdns_server_request_add_reply_fp)(struct evdns_server_request *req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data);
typedef int (*evdns_server_request_add_a_reply_fp)(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl);
typedef int (*evdns_server_request_add_ptr_reply_fp)(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl);
typedef int (*evdns_server_request_respond_fp)(struct evdns_server_request *req, int err);
typedef int (*evdns_server_request_get_requesting_addr_fp)(struct evdns_server_request *_req, struct sockaddr *sa, int addr_len);

/* event2/dns_compat.h */
typedef void (*evdns_shutdown_fp)(int fail_requests);
typedef int (*evdns_nameserver_ip_add_fp)(const char *ip_as_string);
typedef int (*evdns_set_option_fp)(const char *option, const char *val, int flags);
typedef int (*evdns_resolv_conf_parse_fp)(int flags, const char *const filename);

/* save static pointers so we dont have to search on every call */
static event_base_new_fp _event_base_new = NULL;
static event_base_new_with_config_fp _event_base_new_with_config = NULL;
static event_base_free_fp _event_base_free = NULL;
static event_base_get_method_fp _event_base_get_method = NULL;
static event_set_log_callback_fp _event_set_log_callback = NULL;
static event_base_loop_fp _event_base_loop = NULL;
static event_base_loopexit_fp _event_base_loopexit = NULL;
static event_assign_fp _event_assign = NULL;
static event_new_fp _event_new = NULL;
static event_free_fp _event_free = NULL;
static event_add_fp _event_add = NULL;
static event_del_fp _event_del = NULL;
static event_active_fp _event_active = NULL;
static event_pending_fp _event_pending = NULL;
static event_get_version_fp _event_get_version = NULL;
static event_get_version_number_fp _event_get_version_number = NULL;

static evdns_base_new_fp _evdns_base_new = NULL;
static evdns_err_to_string_fp _evdns_err_to_string = NULL;
static evdns_base_count_nameservers_fp _evdns_base_count_nameservers = NULL;
static evdns_base_clear_nameservers_and_suspend_fp _evdns_base_clear_nameservers_and_suspend = NULL;
static evdns_base_resume_fp _evdns_base_resume = NULL;
static evdns_base_resolve_ipv4_fp _evdns_base_resolve_ipv4 = NULL;
static evdns_base_resolve_reverse_fp _evdns_base_resolve_reverse = NULL;
static evdns_base_resolve_reverse_ipv6_fp _evdns_base_resolve_reverse_ipv6 = NULL;
static evdns_base_set_option_fp _evdns_base_set_option = NULL;
static evdns_base_resolv_conf_parse_fp _evdns_base_resolv_conf_parse = NULL;
static evdns_base_search_clear_fp _evdns_base_search_clear = NULL;
static evdns_set_log_fn_fp _evdns_set_log_fn = NULL;
static evdns_set_random_bytes_fn_fp _evdns_set_random_bytes_fn = NULL;
static evdns_add_server_port_with_base_fp _evdns_add_server_port_with_base = NULL;
static evdns_close_server_port_fp _evdns_close_server_port = NULL;
static evdns_server_request_add_reply_fp _evdns_server_request_add_reply = NULL;
static evdns_server_request_add_a_reply_fp _evdns_server_request_add_a_reply = NULL;
static evdns_server_request_add_ptr_reply_fp _evdns_server_request_add_ptr_reply = NULL;
static evdns_server_request_respond_fp _evdns_server_request_respond = NULL;
static evdns_server_request_get_requesting_addr_fp _evdns_server_request_get_requesting_addr = NULL;

static evdns_shutdown_fp _evdns_shutdown = NULL;
static evdns_nameserver_ip_add_fp _evdns_nameserver_ip_add = NULL;
static evdns_set_option_fp _evdns_set_option = NULL;
static evdns_resolv_conf_parse_fp _evdns_resolv_conf_parse = NULL;

/* event2/event.h */
struct event_base *event_base_new(void) {
	event_base_new_fp* fp_ptr = &_event_base_new;
	char* f_name = EVENT_LIB_PREFIX "event_base_new";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)();
}

struct event_base *event_base_new_with_config(const struct event_config *cfg) {
	event_base_new_with_config_fp* fp_ptr = &_event_base_new_with_config;
	char* f_name = EVENT_LIB_PREFIX "event_base_new_with_config";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(cfg);
}

void event_base_free(struct event_base * eb) {
	event_base_free_fp* fp_ptr = &_event_base_free;
	char* f_name = EVENT_LIB_PREFIX "event_base_free";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	return (*fp_ptr)(eb);
}

const char *event_base_get_method(const struct event_base * eb) {
	event_base_get_method_fp* fp_ptr = &_event_base_get_method;
	char* f_name = EVENT_LIB_PREFIX "event_base_get_method";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(eb);
}

void event_set_log_callback(event_log_cb cb) {
	event_set_log_callback_fp* fp_ptr = &_event_set_log_callback;
	char* f_name = EVENT_LIB_PREFIX "event_set_log_callback";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(cb);
}

int event_base_loop(struct event_base * eb, int flags) {
	event_base_loop_fp* fp_ptr = &_event_base_loop;
	char* f_name = EVENT_LIB_PREFIX "event_base_loop";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(eb, flags);
}

int event_base_loopexit(struct event_base * eb, const struct timeval * tv) {
	event_base_loopexit_fp* fp_ptr = &_event_base_loopexit;
	char* f_name = EVENT_LIB_PREFIX "event_base_loopexit";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(eb, tv);
}

int event_assign(struct event * ev, struct event_base * eb, evutil_socket_t sd, short types, event_callback_fn fn, void * arg) {
	event_assign_fp* fp_ptr = &_event_assign;
	char* f_name = EVENT_LIB_PREFIX "event_assign";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(ev, eb, sd, types, fn, arg);
}

struct event *event_new(struct event_base * eb, evutil_socket_t fd, short types, event_callback_fn cb, void * arg) {
	event_new_fp* fp_ptr = &_event_new;
	char* f_name = EVENT_LIB_PREFIX "event_new";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(eb, fd, types, cb, arg);
}

void event_free(struct event * ev) {
	event_free_fp* fp_ptr = &_event_free;
	char* f_name = EVENT_LIB_PREFIX "event_free";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	return (*fp_ptr)(ev);
}

int event_add(struct event * ev, const struct timeval * tv) {
	event_add_fp* fp_ptr = &_event_add;
	char* f_name = EVENT_LIB_PREFIX "event_add";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(ev, tv);
}

int event_del(struct event * ev) {
	event_del_fp* fp_ptr = &_event_del;
	char* f_name = EVENT_LIB_PREFIX "event_del";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(ev);
}

void event_active(struct event * ev, int flags_for_cb, short ncalls) {
	event_active_fp* fp_ptr = &_event_active;
	char* f_name = EVENT_LIB_PREFIX "event_active";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	return (*fp_ptr)(ev, flags_for_cb, ncalls);
}

int event_pending(const struct event * ev, short types, struct timeval * tv) {
	event_pending_fp* fp_ptr = &_event_pending;
	char* f_name = EVENT_LIB_PREFIX "event_pending";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(ev, types, tv);
}

const char *event_get_version(void) {
	event_get_version_fp* fp_ptr = &_event_get_version;

	/* just get the version from libevent */
//	char* f_name = EVENT_LIB_PREFIX "event_get_version";
	char* f_name = "event_get_version";

	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)();
}

ev_uint32_t event_get_version_number(void) {
	event_get_version_number_fp* fp_ptr = &_event_get_version_number;

	/* just get the version from libevent */
//	char* f_name = EVENT_LIB_PREFIX "event_get_version_number";
	char* f_name = "event_get_version_number";

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)();
}

/* event2/dns.h */
struct evdns_base * evdns_base_new(struct event_base *event_base, int initialize_nameservers) {
	evdns_base_new_fp* fp_ptr = &_evdns_base_new;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_new";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(event_base, initialize_nameservers);
}

const char *evdns_err_to_string(int err) {
	evdns_err_to_string_fp* fp_ptr = &_evdns_err_to_string;
	char* f_name = EVENT_LIB_PREFIX "evdns_err_to_string";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(err);
}

int evdns_base_count_nameservers(struct evdns_base *base) {
	evdns_base_count_nameservers_fp* fp_ptr = &_evdns_base_count_nameservers;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_count_nameservers";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base);
}

int evdns_base_clear_nameservers_and_suspend(struct evdns_base *base) {
	evdns_base_clear_nameservers_and_suspend_fp* fp_ptr = &_evdns_base_clear_nameservers_and_suspend;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_clear_nameservers_and_suspend";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base);
}

int evdns_base_resume(struct evdns_base *base) {
	evdns_base_resume_fp* fp_ptr = &_evdns_base_resume;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_resume";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base);
}

struct evdns_request *evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr) {
	evdns_base_resolve_ipv4_fp* fp_ptr = &_evdns_base_resolve_ipv4;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_resolve_ipv4";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(base, name, flags, callback, ptr);
}

struct evdns_request *evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	evdns_base_resolve_reverse_fp* fp_ptr = &_evdns_base_resolve_reverse;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_resolve_reverse";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(base, in, flags, callback, ptr);
}

struct evdns_request *evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	evdns_base_resolve_reverse_ipv6_fp* fp_ptr = &_evdns_base_resolve_reverse_ipv6;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_resolve_reverse_ipv6";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(base, in, flags, callback, ptr);
}

int evdns_base_set_option(struct evdns_base *base, const char *option, const char *val) {
	evdns_base_set_option_fp* fp_ptr = &_evdns_base_set_option;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_set_option";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base, option, val);
}

int evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *const filename) {
	evdns_base_resolv_conf_parse_fp* fp_ptr = &_evdns_base_resolv_conf_parse;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_resolv_conf_parse";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(base, flags, filename);
}

void evdns_base_search_clear(struct evdns_base *base) {
	evdns_base_search_clear_fp* fp_ptr = &_evdns_base_search_clear;
	char* f_name = EVENT_LIB_PREFIX "evdns_base_search_clear";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(base);
}

void evdns_set_log_fn(evdns_debug_log_fn_type fn) {
	evdns_set_log_fn_fp* fp_ptr = &_evdns_set_log_fn;
	char* f_name = EVENT_LIB_PREFIX "evdns_set_log_fn";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(fn);
}

void evdns_set_random_bytes_fn(void (*fn)(char *, size_t)) {
	evdns_set_random_bytes_fn_fp* fp_ptr = &_evdns_set_random_bytes_fn;
	char* f_name = EVENT_LIB_PREFIX "evdns_set_random_bytes_fn";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(fn);
}

struct evdns_server_port *evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data) {
	evdns_add_server_port_with_base_fp* fp_ptr = &_evdns_add_server_port_with_base;
	char* f_name = EVENT_LIB_PREFIX "evdns_add_server_port_with_base";
	PRELOAD_LOOKUP(fp_ptr, f_name, NULL);
	return (*fp_ptr)(base, socket, flags, callback, user_data);
}

void evdns_close_server_port(struct evdns_server_port *port) {
	evdns_close_server_port_fp* fp_ptr = &_evdns_close_server_port;
	char* f_name = EVENT_LIB_PREFIX "evdns_close_server_port";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(port);
}

int evdns_server_request_add_reply(struct evdns_server_request *req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data) {
	evdns_server_request_add_reply_fp* fp_ptr = &_evdns_server_request_add_reply;
	char* f_name = EVENT_LIB_PREFIX "evdns_server_request_add_reply";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(req, section, name, type, dns_class, ttl, datalen, is_name, data);
}

int evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl) {
	evdns_server_request_add_a_reply_fp* fp_ptr = &_evdns_server_request_add_a_reply;
	char* f_name = EVENT_LIB_PREFIX "evdns_server_request_add_a_reply";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(req, name, n, addrs, ttl);
}

int evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl) {
	evdns_server_request_add_ptr_reply_fp* fp_ptr = &_evdns_server_request_add_ptr_reply;
	char* f_name = EVENT_LIB_PREFIX "evdns_server_request_add_ptr_reply";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(req, in, inaddr_name, hostname, ttl);
}

int evdns_server_request_respond(struct evdns_server_request *req, int err) {
	evdns_server_request_respond_fp* fp_ptr = &_evdns_server_request_respond;
	char* f_name = EVENT_LIB_PREFIX "evdns_server_request_respond";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(req, err);
}

int evdns_server_request_get_requesting_addr(struct evdns_server_request *_req, struct sockaddr *sa, int addr_len) {
	evdns_server_request_get_requesting_addr_fp* fp_ptr = &_evdns_server_request_get_requesting_addr;
	char* f_name = EVENT_LIB_PREFIX "evdns_server_request_get_requesting_addr";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(_req, sa, addr_len);
}


/* event2/dns_compat.h */
void evdns_shutdown(int fail_requests) {
	evdns_shutdown_fp* fp_ptr = &_evdns_shutdown;
	char* f_name = EVENT_LIB_PREFIX "evdns_shutdown";
	PRELOAD_LOOKUP(fp_ptr, f_name,);
	(*fp_ptr)(fail_requests);
}

int evdns_nameserver_ip_add(const char *ip_as_string) {
	evdns_nameserver_ip_add_fp* fp_ptr = &_evdns_nameserver_ip_add;
	char* f_name = EVENT_LIB_PREFIX "evdns_nameserver_ip_add";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(ip_as_string);
}

int evdns_set_option(const char *option, const char *val, int flags) {
	evdns_set_option_fp* fp_ptr = &_evdns_set_option;
	char* f_name = EVENT_LIB_PREFIX "evdns_set_option";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(option, val, flags);
}

int evdns_resolv_conf_parse(int flags, const char *const filename) {
	evdns_resolv_conf_parse_fp* fp_ptr = &_evdns_resolv_conf_parse;
	char* f_name = EVENT_LIB_PREFIX "evdns_resolv_conf_parse";
	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(flags, filename);
}
