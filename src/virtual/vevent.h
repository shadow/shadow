/*
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

#ifndef VEVENT_H_
#define VEVENT_H_

#include <glib.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "shadow.h"

/* vevent types and functions */
#define VEVENT_METHOD "shadow-vevent";

/* represents a descriptor we are monitoring */
struct vevent_socket_s {
	gint sd;
	GQueue *vevents;
};

/* wrapper around an event */
struct vevent_s {
	gint id;
	event_tp event;
	vevent_socket_tp vsd;
	gint timerid;
	gint ntimers;
};

struct vevent_timer_s {
	vevent_mgr_tp mgr;
	vevent_tp vev;
};

void vevent_notify(vevent_mgr_tp mgr, gint sockd, short event_type);
void vevent_destroy_base(vevent_mgr_tp mgr, event_base_tp eb);
gchar* vevent_get_event_type_string(vevent_mgr_tp mgr, short event_type);

/* libevent intercepted functions */

/* event2/event.h */
event_base_tp vevent_event_base_new(vevent_mgr_tp mgr);
event_base_tp vevent_event_base_new_with_config(vevent_mgr_tp mgr, const struct event_config *cfg);
void vevent_event_base_free(vevent_mgr_tp mgr, event_base_tp eb);
const gchar* vevent_event_base_get_method(vevent_mgr_tp mgr, const event_base_tp);
void vevent_event_set_log_callback(vevent_mgr_tp mgr, event_log_cb cb);
gint vevent_event_base_loop(vevent_mgr_tp mgr, event_base_tp, gint);
gint vevent_event_base_loopexit(vevent_mgr_tp mgr, event_base_tp, const struct timeval *);
gint vevent_event_assign(vevent_mgr_tp mgr, event_tp, event_base_tp, evutil_socket_t, short, event_callback_fn, gpointer );
event_tp vevent_event_new(vevent_mgr_tp mgr, event_base_tp, evutil_socket_t, short, event_callback_fn, gpointer );
void vevent_event_free(vevent_mgr_tp mgr, event_tp);
gint vevent_event_add(vevent_mgr_tp mgr, event_tp, const struct timeval *);
gint vevent_event_del(vevent_mgr_tp mgr, event_tp);
void vevent_event_active(vevent_mgr_tp mgr, event_tp, gint, short);
gint vevent_event_pending(vevent_mgr_tp mgr, const event_tp, short, struct timeval *);

/* event2/util.h */
/* (for evutil_socket_t) */

/* event2/dns.h */
evdns_base_tp vevent_evdns_base_new(event_base_tp event_base, gint initialize_nameservers);
const gchar *vevent_evdns_err_to_string(gint err);
gint vevent_evdns_base_count_nameservers(evdns_base_tp base);
gint vevent_evdns_base_clear_nameservers_and_suspend(evdns_base_tp base);
gint vevent_evdns_base_resume(evdns_base_tp base);
evdns_request_tp vevent_evdns_base_resolve_ipv4(evdns_base_tp base, const gchar *name, gint flags, evdns_callback_type callback, gpointer ptr);
evdns_request_tp vevent_evdns_base_resolve_reverse(evdns_base_tp base, const struct in_addr *in, gint flags, evdns_callback_type callback, gpointer ptr);
evdns_request_tp vevent_evdns_base_resolve_reverse_ipv6(evdns_base_tp base, const struct in6_addr *in, gint flags, evdns_callback_type callback, gpointer ptr);
gint vevent_evdns_base_set_option(evdns_base_tp base, const gchar *option, const gchar *val);
gint vevent_evdns_base_resolv_conf_parse(evdns_base_tp base, gint flags, const gchar *const filename);
void vevent_evdns_base_search_clear(evdns_base_tp base);
void vevent_evdns_set_log_fn(evdns_debug_log_fn_type fn);
void vevent_evdns_set_random_bytes_fn(void (*fn)(gchar *, size_t));
evdns_server_port_tp vevent_evdns_add_server_port_with_base(event_base_tp base, evutil_socket_t socket, gint flags, evdns_request_callback_fn_type callback, gpointer user_data);
void vevent_evdns_close_server_port(evdns_server_port_tp port);
gint vevent_evdns_server_request_add_reply(evdns_server_request_tp req, gint section, const gchar *name, gint type, gint dns_class, gint ttl, gint datalen, gint is_name, const gchar *data);
gint vevent_evdns_server_request_add_a_reply(evdns_server_request_tp req, const gchar *name, gint n, const gpointer addrs, gint ttl);
gint vevent_evdns_server_request_add_ptr_reply(evdns_server_request_tp req, struct in_addr *in, const gchar *inaddr_name, const gchar *hostname, gint ttl);
gint vevent_evdns_server_request_respond(evdns_server_request_tp req, gint err);
gint vevent_evdns_server_request_get_requesting_addr(evdns_server_request_tp _req, struct sockaddr *sa, gint addr_len);

/* event2/dns_compat.h */
void vevent_evdns_shutdown(gint fail_requests);
gint vevent_evdns_nameserver_ip_add(const gchar *ip_as_string);
gint vevent_evdns_set_option(const gchar *option, const gchar *val, gint flags);
gint vevent_evdns_resolv_conf_parse(gint flags, const gchar *const filename);

#endif /* VEVENT_H_ */
