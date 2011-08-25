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

#ifndef VEVENT_H_
#define VEVENT_H_

#include <sys/time.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "vevent_mgr.h"
#include "context.h"

/* vevent types and functions */
#define VEVENT_METHOD "shadow-vevent";

/* represents a descriptor we are monitoring */
typedef struct vevent_socket_s {
	int sd;
	GQueue *vevents;
} vevent_socket_t, *vevent_socket_tp;

/* wrapper around an event */
typedef struct vevent_s {
	int id;
	event_tp event;
	vevent_socket_tp vsd;
	int timerid;
	int ntimers;
} vevent_t, *vevent_tp;

typedef struct vevent_timer_s {
	vevent_mgr_tp mgr;
	vevent_tp vev;
} vevent_timer_t, *vevent_timer_tp;

void vevent_notify(vevent_mgr_tp mgr, int sockd, short event_type);
void vevent_destroy_base(vevent_mgr_tp mgr, event_base_tp eb);
char* vevent_get_event_type_string(vevent_mgr_tp mgr, short event_type);

/* libevent intercepted functions */

/* event2/event.h */
event_base_tp vevent_event_base_new(vevent_mgr_tp mgr);
event_base_tp vevent_event_base_new_with_config(vevent_mgr_tp mgr, const struct event_config *cfg);
void vevent_event_base_free(vevent_mgr_tp mgr, event_base_tp eb);
const char *vevent_event_base_get_method(vevent_mgr_tp mgr, const event_base_tp);
void vevent_event_set_log_callback(vevent_mgr_tp mgr, event_log_cb cb);
int vevent_event_base_loop(vevent_mgr_tp mgr, event_base_tp, int);
int vevent_event_base_loopexit(vevent_mgr_tp mgr, event_base_tp, const struct timeval *);
int vevent_event_assign(vevent_mgr_tp mgr, event_tp, event_base_tp, evutil_socket_t, short, event_callback_fn, void *);
event_tp vevent_event_new(vevent_mgr_tp mgr, event_base_tp, evutil_socket_t, short, event_callback_fn, void *);
void vevent_event_free(vevent_mgr_tp mgr, event_tp);
int vevent_event_add(vevent_mgr_tp mgr, event_tp, const struct timeval *);
int vevent_event_del(vevent_mgr_tp mgr, event_tp);
void vevent_event_active(vevent_mgr_tp mgr, event_tp, int, short);
int vevent_event_pending(vevent_mgr_tp mgr, const event_tp, short, struct timeval *);

/* event2/util.h */
/* (for evutil_socket_t) */

/* event2/dns.h */
evdns_base_tp vevent_evdns_base_new(event_base_tp event_base, int initialize_nameservers);
const char *vevent_evdns_err_to_string(int err);
int vevent_evdns_base_count_nameservers(evdns_base_tp base);
int vevent_evdns_base_clear_nameservers_and_suspend(evdns_base_tp base);
int vevent_evdns_base_resume(evdns_base_tp base);
evdns_request_tp vevent_evdns_base_resolve_ipv4(evdns_base_tp base, const char *name, int flags, evdns_callback_type callback, void *ptr);
evdns_request_tp vevent_evdns_base_resolve_reverse(evdns_base_tp base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);
evdns_request_tp vevent_evdns_base_resolve_reverse_ipv6(evdns_base_tp base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);
int vevent_evdns_base_set_option(evdns_base_tp base, const char *option, const char *val);
int vevent_evdns_base_resolv_conf_parse(evdns_base_tp base, int flags, const char *const filename);
void vevent_evdns_base_search_clear(evdns_base_tp base);
void vevent_evdns_set_log_fn(evdns_debug_log_fn_type fn);
void vevent_evdns_set_random_bytes_fn(void (*fn)(char *, size_t));
evdns_server_port_tp vevent_evdns_add_server_port_with_base(event_base_tp base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data);
void vevent_evdns_close_server_port(evdns_server_port_tp port);
int vevent_evdns_server_request_add_reply(evdns_server_request_tp req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data);
int vevent_evdns_server_request_add_a_reply(evdns_server_request_tp req, const char *name, int n, const void *addrs, int ttl);
int vevent_evdns_server_request_add_ptr_reply(evdns_server_request_tp req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl);
int vevent_evdns_server_request_respond(evdns_server_request_tp req, int err);
int vevent_evdns_server_request_get_requesting_addr(evdns_server_request_tp _req, struct sockaddr *sa, int addr_len);

/* event2/dns_compat.h */
void vevent_evdns_shutdown(int fail_requests);
int vevent_evdns_nameserver_ip_add(const char *ip_as_string);
int vevent_evdns_set_option(const char *option, const char *val, int flags);
int vevent_evdns_resolv_conf_parse(int flags, const char *const filename);

#endif /* VEVENT_H_ */
