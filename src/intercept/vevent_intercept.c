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

/**
 * This file contains methods that are called from the preloaded library. These
 * functions must exist in a shared library so that the preloaded lib can
 * properly search for them with dlsym.
 *
 * This file is unnecessary if the functions called exist in a module, since
 * the module is already a shared library.
 */

#include <glib.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "shadow.h"

/* event2/event.h */
struct event_base *intercept_event_base_new(void) {
	INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
			struct event_base * r = vevent_event_base_new(mgr),
			return r);
}

struct event_base *intercept_event_base_new_with_config(const struct event_config *cfg) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		struct event_base * r = vevent_event_base_new_with_config(mgr, cfg),
			return r);
}

void intercept_event_base_free(struct event_base * eb) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		vevent_event_base_free(mgr, eb),);
}

const gchar *intercept_event_base_get_method(const struct event_base * eb) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		const gchar * r = vevent_event_base_get_method(mgr, (const event_base_tp)eb),
			return r);
}

void intercept_event_set_log_callback(event_log_cb cb) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		vevent_event_set_log_callback(mgr, cb),);
}

gint intercept_event_base_loop(struct event_base * eb, gint flags) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_base_loop(mgr, eb, flags),
			return r);
}

gint intercept_event_base_loopexit(struct event_base * eb, const struct timeval * tv) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_base_loopexit(mgr, eb, tv),
			return r);
}

struct event *intercept_event_new(struct event_base * eb, evutil_socket_t fd, short types, event_callback_fn cb, gpointer arg) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		struct event * r = vevent_event_new(mgr, eb, fd, types, cb, arg),
			return r);
}

void intercept_event_free(struct event * ev) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		vevent_event_free(mgr, ev),);
}

gint intercept_event_assign(struct event * ev, struct event_base * eb, evutil_socket_t sd, short types, event_callback_fn fn, gpointer arg) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_assign(mgr, ev, eb, sd, types, fn, arg),
			return r);
}

gint intercept_event_add(struct event * ev, const struct timeval * tv) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_add(mgr, ev, tv),
			return r);
}

gint intercept_event_del(struct event * ev) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_del(mgr, ev),
			return r);
}

void intercept_event_active(struct event * ev, gint flags_for_cb, short ncalls) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		vevent_event_active(mgr, ev, flags_for_cb, ncalls),);
}

gint intercept_event_pending(const struct event * ev, short types, struct timeval * tv) {
    INTERCEPT_CONTEXT_SWITCH(vevent_mgr_tp mgr = w->cached_node->vsocket_mgr->vev_mgr,
    		gint r = vevent_event_pending(mgr, (const event_tp) ev, types, tv),
			return r);
}

const gchar *intercept_event_get_version(void) {
	return "0";
}

guint32 intercept_event_get_version_number(void) {
	return 0;
}


/* event2/dns.h */
struct evdns_base * intercept_evdns_base_new(struct event_base *event_base, gint initialize_nameservers) {
    INTERCEPT_CONTEXT_SWITCH(,
    		struct evdns_base * r = vevent_evdns_base_new(event_base, initialize_nameservers),
			return r);
}

const gchar *intercept_evdns_err_to_string(gint err) {
    INTERCEPT_CONTEXT_SWITCH(,
    		const gchar * r = vevent_evdns_err_to_string(err),
			return r);
}

gint intercept_evdns_base_count_nameservers(struct evdns_base *base) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_base_count_nameservers(base),
			return r);
}

gint intercept_evdns_base_clear_nameservers_and_suspend(struct evdns_base *base) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_base_clear_nameservers_and_suspend(base),
			return r);
}

gint intercept_evdns_base_resume(struct evdns_base *base) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_base_resume(base),
			return r);
}

struct evdns_request *intercept_evdns_base_resolve_ipv4(struct evdns_base *base, const gchar *name, gint flags, evdns_callback_type callback, gpointer ptr) {
    INTERCEPT_CONTEXT_SWITCH(,
    		struct evdns_request * r = vevent_evdns_base_resolve_ipv4(base, name, flags, callback, ptr),
			return r);
}

struct evdns_request *intercept_evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, gint flags, evdns_callback_type callback, gpointer ptr) {
    INTERCEPT_CONTEXT_SWITCH(,
    		struct evdns_request * r = vevent_evdns_base_resolve_reverse(base, in, flags, callback, ptr),
			return r);
}

struct evdns_request *intercept_evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, gint flags, evdns_callback_type callback, gpointer ptr) {
    INTERCEPT_CONTEXT_SWITCH(,
    		struct evdns_request * r = vevent_evdns_base_resolve_reverse_ipv6(base, in, flags, callback, ptr),
			return r);
}

gint intercept_evdns_base_set_option(struct evdns_base *base, const gchar *option, const gchar *val) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_base_set_option(base, option, val),
			return r);
}

gint intercept_evdns_base_resolv_conf_parse(struct evdns_base *base, gint flags, const gchar *const filename) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_base_resolv_conf_parse(base, flags, filename),
			return r);
}

void intercept_evdns_base_search_clear(struct evdns_base *base) {
    INTERCEPT_CONTEXT_SWITCH(,
    		vevent_evdns_base_search_clear(base), );
}

void intercept_evdns_set_log_fn(evdns_debug_log_fn_type fn) {
    INTERCEPT_CONTEXT_SWITCH(,
    		vevent_evdns_set_log_fn(fn),);
}

void intercept_evdns_set_random_bytes_fn(void (*fn)(gchar *, size_t)) {
    INTERCEPT_CONTEXT_SWITCH(,
    		vevent_evdns_set_random_bytes_fn(fn),);
}

struct evdns_server_port *intercept_evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, gint flags, evdns_request_callback_fn_type callback, gpointer user_data) {
    INTERCEPT_CONTEXT_SWITCH(,
    		struct evdns_server_port * r = vevent_evdns_add_server_port_with_base(base, socket, flags, callback, user_data),
			return r);
}

void intercept_evdns_close_server_port(struct evdns_server_port *port) {
    INTERCEPT_CONTEXT_SWITCH(,
    		vevent_evdns_close_server_port(port),);
}

gint intercept_evdns_server_request_add_reply(struct evdns_server_request *req, gint section, const gchar *name, gint type, gint dns_class, gint ttl, gint datalen, gint is_name, const gchar *data) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_server_request_add_reply(req, section, name, type, dns_class, ttl, datalen, is_name, data),
			return r);
}

gint intercept_evdns_server_request_add_a_reply(struct evdns_server_request *req, const gchar *name, gint n, const gpointer addrs, gint ttl) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_server_request_add_a_reply(req, name, n, addrs, ttl),
			return r);
}

gint intercept_evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const gchar *inaddr_name, const gchar *hostname, gint ttl) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_server_request_add_ptr_reply(req, in, inaddr_name, hostname, ttl),
			return r);
}

gint intercept_evdns_server_request_respond(struct evdns_server_request *req, gint err) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_server_request_respond(req, err),
			return r);
}

gint intercept_evdns_server_request_get_requesting_addr(struct evdns_server_request *_req, struct sockaddr *sa, gint addr_len) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_server_request_get_requesting_addr(_req, sa, addr_len),
			return r);
}


/* event2/dns_compat.h */
void intercept_evdns_shutdown(gint fail_requests) {
    INTERCEPT_CONTEXT_SWITCH(,
    		vevent_evdns_shutdown(fail_requests),);
}

gint intercept_evdns_nameserver_ip_add(const gchar *ip_as_string) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_nameserver_ip_add(ip_as_string),
			return r);
}

gint intercept_evdns_set_option(const gchar *option, const gchar *val, gint flags) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_set_option(option, val, flags),
			return r);
}

gint intercept_evdns_resolv_conf_parse(gint flags, const gchar *const filename) {
    INTERCEPT_CONTEXT_SWITCH(,
    		gint r = vevent_evdns_resolv_conf_parse(flags, filename),
			return r);
}

