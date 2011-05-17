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

#ifndef intercept_H_
#define intercept_H_

/**
 * This file lists all the functions scallion intercepts from libevent. This list
 * contains all libevent functions that Tor calls as of 0.2.2.15-alpha.
 */

#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

/* event2/event.h */
struct event_base *intercept_event_base_new(void);
void intercept_event_base_free(struct event_base *);
const char *intercept_event_base_get_method(const struct event_base *);
void intercept_event_set_log_callback(event_log_cb cb);
int intercept_event_base_loop(struct event_base *, int);
int intercept_event_base_loopexit(struct event_base *, const struct timeval *);
struct event *intercept_event_new(struct event_base *, evutil_socket_t, short, event_callback_fn, void *);
void intercept_event_free(struct event *);
int intercept_event_assign(struct event * ev, struct event_base * eb, evutil_socket_t sd, short types, event_callback_fn fn, void * arg);
int intercept_event_add(struct event *, const struct timeval *);
int intercept_event_del(struct event *);
void intercept_event_active(struct event *, int, short);
int intercept_event_pending(const struct event *, short, struct timeval *);
//const char *intercept_event_get_version(void);
//ev_uint32_t intercept_event_get_version_number(void);

/* event2/util.h */
/* (for evutil_socket_t) */

/* event2/dns.h */
struct evdns_base * intercept_evdns_base_new(struct event_base *event_base, int initialize_nameservers);
const char *intercept_evdns_err_to_string(int err);
int intercept_evdns_base_count_nameservers(struct evdns_base *base);
int intercept_evdns_base_clear_nameservers_and_suspend(struct evdns_base *base);
int intercept_evdns_base_resume(struct evdns_base *base);
struct evdns_request *intercept_evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags, evdns_callback_type callback, void *ptr);
struct evdns_request *intercept_evdns_base_resolve_reverse(struct evdns_base *base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr);
struct evdns_request *intercept_evdns_base_resolve_reverse_ipv6(struct evdns_base *base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr);
int intercept_evdns_base_set_option(struct evdns_base *base, const char *option, const char *val);
int intercept_evdns_base_resolv_conf_parse(struct evdns_base *base, int flags, const char *const filename);
void intercept_evdns_base_search_clear(struct evdns_base *base);
void intercept_evdns_set_log_fn(evdns_debug_log_fn_type fn);
void intercept_evdns_set_random_bytes_fn(void (*fn)(char *, size_t));
struct evdns_server_port *intercept_evdns_add_server_port_with_base(struct event_base *base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data);
void intercept_evdns_close_server_port(struct evdns_server_port *port);
int intercept_evdns_server_request_add_reply(struct evdns_server_request *req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data);
int intercept_evdns_server_request_add_a_reply(struct evdns_server_request *req, const char *name, int n, const void *addrs, int ttl);
int intercept_evdns_server_request_add_ptr_reply(struct evdns_server_request *req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl);
int intercept_evdns_server_request_respond(struct evdns_server_request *req, int err);
int intercept_evdns_server_request_get_requesting_addr(struct evdns_server_request *_req, struct sockaddr *sa, int addr_len);

/* event2/dns_compat.h */
void intercept_evdns_shutdown(int fail_requests);
int intercept_evdns_nameserver_ip_add(const char *ip_as_string);
int intercept_evdns_set_option(const char *option, const char *val, int flags);
int intercept_evdns_resolv_conf_parse(int flags, const char *const filename);

/* event2/dns_struct.h */
/* (for evdns_server_request and evdns_server_question) */

#endif /* intercept_H_ */
