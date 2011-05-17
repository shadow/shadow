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

/**
 * This file contains methods that are called from the preloaded library. These
 * functions must exist in a shared library so that the preloaded lib can
 * properly search for them with dlsym.
 *
 * This file is unnecessary if the functions called exist in a module, since
 * the module is already a shared library.
 */

#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "vevent_intercept.h"
#include "vevent.h"
#include "context.h"
#include "vsocket_mgr.h"

static vevent_mgr_tp get_vevent_mgr(){
	return (vevent_mgr_tp) global_sim_context.current_context->vsocket_mgr->vev_mgr;
}

/* event2/event.h */
struct event_base *intercept_event_base_new(void) {
	return vevent_event_base_new(get_vevent_mgr());
}

void intercept_event_base_free(struct event_base * eb) {
	vevent_event_base_free(get_vevent_mgr(), eb);
}

const char *intercept_event_base_get_method(const struct event_base * eb) {
	return vevent_event_base_get_method(get_vevent_mgr(), (const event_base_tp)eb);
}

void intercept_event_set_log_callback(event_log_cb cb) {
	vevent_event_set_log_callback(get_vevent_mgr(), cb);
}

int intercept_event_base_loop(struct event_base * eb, int flags) {
	return vevent_event_base_loop(get_vevent_mgr(), eb, flags);
}

int intercept_event_base_loopexit(struct event_base * eb, const struct timeval * tv) {
	return vevent_event_base_loopexit(get_vevent_mgr(), eb, tv);
}

struct event *intercept_event_new(struct event_base * eb, evutil_socket_t fd, short types, event_callback_fn cb, void * arg) {
	return vevent_event_new(get_vevent_mgr(), eb, fd, types, cb, arg);
}

void intercept_event_free(struct event * ev) {
	vevent_event_free(get_vevent_mgr(), ev);
}

int intercept_event_assign(struct event * ev, struct event_base * eb, evutil_socket_t sd, short types, event_callback_fn fn, void * arg) {
	return vevent_event_assign(get_vevent_mgr(), ev, eb, sd, types, fn, arg);
}

int intercept_event_add(struct event * ev, const struct timeval * tv) {
	return vevent_event_add(get_vevent_mgr(), ev, tv);
}

int intercept_event_del(struct event * ev) {
	return vevent_event_del(get_vevent_mgr(), ev);
}

void intercept_event_active(struct event * ev, int flags_for_cb, short ncalls) {
	vevent_event_active(get_vevent_mgr(), ev, flags_for_cb, ncalls);
}

int intercept_event_pending(const struct event * ev, short types, struct timeval * tv) {
	return vevent_event_pending(get_vevent_mgr(), (const event_tp) ev, types, tv);
}

//const char *intercept_event_get_version(void) {
//	return vevent_event_get_version(get_vevent_mgr());
//}
//
//ev_uint32_t intercept_event_get_version_number(void) {
//	return vevent_event_get_version_number(get_vevent_mgr());
//}


/* event2/dns.h */
struct evdns_base * intercept_evdns_base_new(struct event_base *event_base, int initialize_nameservers) {
	return vevent_evdns_base_new(event_base, initialize_nameservers);
}

const char *intercept_evdns_err_to_string(int err) {
	return vevent_evdns_err_to_string(err);
}

int intercept_evdns_base_count_nameservers(struct evdns_base *base) {
	return vevent_evdns_base_count_nameservers(base);
}

int intercept_evdns_base_clear_nameservers_and_suspend(struct evdns_base *base) {
	return vevent_evdns_base_clear_nameservers_and_suspend(base);
}

int intercept_evdns_base_resume(struct evdns_base *base) {
	return vevent_evdns_base_resume(base);
}

struct evdns_request *intercept_evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr) {
	return vevent_evdns_base_resolve_ipv4(base, name, flags, callback, ptr);
}

struct evdns_request *intercept_evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	return vevent_evdns_base_resolve_reverse(base, in, flags, callback, ptr);
}

struct evdns_request *intercept_evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	return vevent_evdns_base_resolve_reverse_ipv6(base, in, flags, callback, ptr);
}

int intercept_evdns_base_set_option(struct evdns_base *base, const char *option, const char *val) {
	return vevent_evdns_base_set_option(base, option, val);
}

int intercept_evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *const filename) {
	return vevent_evdns_base_resolv_conf_parse(base, flags, filename);
}

void intercept_evdns_base_search_clear(struct evdns_base *base) {
	vevent_evdns_base_search_clear(base);
}

void intercept_evdns_set_log_fn(evdns_debug_log_fn_type fn) {
	vevent_evdns_set_log_fn(fn);
}

void intercept_evdns_set_random_bytes_fn(void (*fn)(char *, size_t)) {
	vevent_evdns_set_random_bytes_fn(fn);
}

struct evdns_server_port *intercept_evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data) {
	return vevent_evdns_add_server_port_with_base(base, socket, flags, callback, user_data);
}

void intercept_evdns_close_server_port(struct evdns_server_port *port) {
	vevent_evdns_close_server_port(port);
}

int intercept_evdns_server_request_add_reply(struct evdns_server_request *req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data) {
	return vevent_evdns_server_request_add_reply(req, section, name, type, dns_class, ttl, datalen, is_name, data);
}

int intercept_evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl) {
	return vevent_evdns_server_request_add_a_reply(req, name, n, addrs, ttl);
}

int intercept_evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl) {
	return vevent_evdns_server_request_add_ptr_reply(req, in, inaddr_name, hostname, ttl);
}

int intercept_evdns_server_request_respond(struct evdns_server_request *req, int err) {
	return vevent_evdns_server_request_respond(req, err);
}

int intercept_evdns_server_request_get_requesting_addr(struct evdns_server_request *_req, struct sockaddr *sa, int addr_len) {
	return vevent_evdns_server_request_get_requesting_addr(_req, sa, addr_len);
}


/* event2/dns_compat.h */
void intercept_evdns_shutdown(int fail_requests) {
	vevent_evdns_shutdown(fail_requests);
}

int intercept_evdns_nameserver_ip_add(const char *ip_as_string) {
	return vevent_evdns_nameserver_ip_add(ip_as_string);
}

int intercept_evdns_set_option(const char *option, const char *val, int flags) {
	return vevent_evdns_set_option(option, val, flags);
}

int intercept_evdns_resolv_conf_parse(int flags, const char *const filename) {
	return vevent_evdns_resolv_conf_parse(flags, filename);
}

