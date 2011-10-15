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

#include <glib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/util.h>
#include <event2/dns.h>
#include <event2/dns_compat.h>
#include <event2/dns_struct.h>

#include "shadow.h"

/* FIXME TODO:
 * -make vevent_mgr accesses local to vevent_mgr.c
 * -refactor to better utilize shadow gintegration
 * -refactor should allow user to remove libevent dependency while still having vevent function for shadow */

static void vevent_execute(vevent_mgr_tp mgr, event_tp ev);
static gint vevent_set_timer(vevent_mgr_tp mgr, vevent_tp vev, const struct timeval *tv);


/* start helper functions */

gchar* vevent_get_event_type_string(vevent_mgr_tp mgr, short event_type) {
	if(mgr != NULL && mgr->typebuf != NULL) {
		gpointer buffer = mgr->typebuf;
		size_t size = sizeof(mgr->typebuf);
		size_t written = 0;

		if(written < size && (event_type & EV_TIMEOUT)) {
			written += snprintf(buffer + written, size - written, "|EV_TIMEOUT");
		}
		if(written < size && (event_type & EV_READ)) {
			written += snprintf(buffer + written, size - written, "|EV_READ");
		}
		if(written < size && (event_type & EV_WRITE)) {
			written += snprintf(buffer + written, size - written, "|EV_WRITE");
		}
		if(written < size && (event_type & EV_SIGNAL)) {
			written += snprintf(buffer + written, size - written, "|EV_SIGNAL");
		}
		if(written < size && (event_type & EV_PERSIST)) {
			written += snprintf(buffer + written, size - written, "|EV_PERSIST");
		}
		if(written < size && (event_type & EV_ET)) {
			written += snprintf(buffer + written, size - written, "|EV_ET");
		}
		if(written < size) {
			written += snprintf(buffer + written, size - written, "|");
		}

		return buffer;
	} else {
		return "EV_TYPE_ERROR!";
	}
}

static void vevent_destroy_socket(vevent_socket_tp vsd) {
	if(vsd != NULL) {
		if(vsd->vevents != NULL) {
			g_queue_free(vsd->vevents);
			vsd->vevents = NULL;
		}
		free(vsd);
	}
}

static void vevent_destroy_socket_cb(gpointer value, gint key) {
	vevent_destroy_socket((vevent_socket_tp)value);
}

static void vevent_destroy_vevent(vevent_tp vev) {
	if(vev != NULL) {
		vev->vsd = NULL;
		free(vev);
	}
}

static void vevent_destroy_vevent_cb(gint key, gpointer value, gpointer param) {
	vevent_destroy_vevent((vevent_tp)value);
}

static void vevent_vepoll_action(guint16 sockd, guint8 add, short ev_type) {
	/* make sure we tell vepoll our preference for event notifications when the socket/pipe is ready */
	Worker* worker = worker_getPrivate();
	vsocket_mgr_tp vsock_mgr = worker->cached_node->vsocket_mgr;
	if(vsock_mgr != NULL) {
		vepoll_tp poll = NULL;

		poll = vpipe_get_poll(vsock_mgr->vpipe_mgr, sockd);
		if(poll == NULL) {
			vsocket_tp sock = vsocket_mgr_get_socket(vsock_mgr, sockd);
			if(sock != NULL) {
				poll = sock->vep;
			}
		}

		enum vepoll_type t = 0;
		if(ev_type & EV_READ) {
			t |= VEPOLL_READ;
		}
		if(ev_type & EV_WRITE) {
			t |= VEPOLL_WRITE;
		}

		if(add) {
			vepoll_vevent_add(poll, t);
		} else {
			vepoll_vevent_delete(poll, t);
		}
	}
}

void vevent_destroy_base(vevent_mgr_tp mgr, event_base_tp eb) {
	if(eb != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
		if(veb != NULL) {
			if(veb->sockets_by_sd != NULL) {
				g_hash_table_foreach(veb->sockets_by_sd, (GHFunc)vevent_destroy_socket_cb, NULL);
				g_hash_table_destroy(veb->sockets_by_sd);
				veb->sockets_by_sd = NULL;
			}

			if(veb->vevents_by_id != NULL) {
				g_hash_table_foreach(veb->vevents_by_id, (GHFunc)vevent_destroy_vevent_cb, NULL);
				g_hash_table_destroy(veb->vevents_by_id);
				veb->vevents_by_id = NULL;
			}

			free(veb);
		}

		free(eb);
	}
}

static vevent_socket_tp vevent_socket_create(gint sd) {
	vevent_socket_tp vsd = malloc(sizeof(vevent_socket_t));
	vsd->vevents = g_queue_new();
	vsd->sd = sd;
	return vsd;
}

static vevent_tp vevent_create(event_tp ev, vevent_socket_tp vsd) {
	if(ev != NULL) {
		vevent_tp vev = malloc(sizeof(vevent_t));
		vev->event = ev;
		vev->id = ev->ev_timeout_pos.min_heap_idx;
		vev->ntimers = 0;
		vev->timerid = 0;
		vev->vsd = vsd;
		return vev;
	}
	return NULL;
}

static void vevent_executeTimerCallback(gpointer data, gpointer argument) {
	vevent_timer_tp vt = data;

	if(vt != NULL && vt->vev != NULL && vt->mgr != NULL) {
		vevent_tp vev = vt->vev;
		event_tp ev = vev->event;

		if(ev != NULL) {
			ev->ev_flags &= ~EVLIST_TIMEOUT;

			/* execute if this timer is still valid */
			if(vev->timerid != -1) {
				vevent_execute(vt->mgr, ev);
			}
		}

		vev->ntimers--;

		/* delete if unregistered and there are no outstanding timers */
		if(vev->ntimers <= 0 && ev == NULL) {
			vevent_destroy_vevent(vev);
		}
		/* persistent timers need to be rescheduled
		 * NOTE: need to check from vev here, and not just ev, to account for changes during execute. */
		else if(vev->event != NULL && ((vev->event->ev_events & EV_PERSIST) == EV_PERSIST)) {
			vevent_set_timer(vt->mgr, vev, &(vev->event->ev_timeout));
		}
	}

	if(vt != NULL) {
		free(vt);
	}
}

static void vevent_timerTimerCallback(gpointer data, gpointer argument) {
	/* need to be in node context */
	Worker* worker = worker_getPrivate();
	Application* a = worker->cached_node->application;
	Plugin* plugin = worker_getPlugin(a->software);
	plugin_executeGeneric(plugin, a->state, vevent_executeTimerCallback, data, argument);
}

static gint vevent_isequal_cb(gpointer ev1, gpointer ev2) {
	if(ev1 != NULL && ev2 != NULL) {
		if((((vevent_tp)ev1)->id) == (((vevent_tp)ev2)->id)) {
			return 0;
		} else if((((vevent_tp)ev1)->id) < (((vevent_tp)ev2)->id)) {
                        return -1;
		} else if((((vevent_tp)ev1)->id) == (((vevent_tp)ev2)->id)) {
                        return 1;
                }
	}
	return 1;
}

static gint vevent_set_timer(vevent_mgr_tp mgr, vevent_tp vev, const struct timeval *tv) {
	if(tv != NULL && mgr != NULL) {
		/* evtimer called with a time - lets do a call-through to shadow timer */
		gint delay_millis = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);

		/* timer create result will be the timerid or an error */
		vevent_timer_tp vt = malloc(sizeof(vevent_timer_t));
		vt->mgr = mgr;
		vt->vev = vev;

		vevent_mgr_timer_create(mgr, delay_millis, &vevent_timerTimerCallback, vt);

		vev->timerid = ++(mgr->id_counter);
		vev->ntimers++;
		vev->event->ev_flags |= EVLIST_TIMEOUT;
		return 0;
	} else {
		critical("timer created without specifying delay. timer event not added. event id %d, fd %d, type %s", vev->id, vev->event->ev_fd, vevent_get_event_type_string(mgr, vev->event->ev_events));
	}

	return -1;
}

static gint vevent_register(vevent_mgr_tp mgr, event_tp ev, const struct timeval * timeout) {
	/* check if the event already is added */
	if(ev != NULL && ev->ev_base != NULL) {

		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {
		/* check for the socket */
		vevent_socket_tp vsd = g_hash_table_lookup(veb->sockets_by_sd, &ev->ev_fd);
		if(vsd == NULL) {
			vsd = vevent_socket_create(ev->ev_fd);
			g_hash_table_insert(veb->sockets_by_sd, gint_key(ev->ev_fd), vsd);
			debug("start monitoring socket %d", ev->ev_fd);
		}

		/* register as event */
		vevent_tp vev = g_hash_table_lookup(veb->vevents_by_id, &ev->ev_timeout_pos.min_heap_idx);
		if(vev == NULL) {
			vev = vevent_create(ev, vsd);
			g_hash_table_insert(veb->vevents_by_id, gint_key(vev->id), vev);
			vev->event->ev_flags |= EVLIST_INSERTED;
			debug("inserted vevent id %d, fd %d, type %s", vev->id, vev->vsd->sd, vevent_get_event_type_string(mgr, vev->event->ev_events));
		}

		/* register with socket */
		if(g_queue_find_custom(vsd->vevents, vev, (GCompareFunc)vevent_isequal_cb) == NULL) {
			g_queue_push_tail(vsd->vevents, vev);
			vevent_vepoll_action(vsd->sd, 1, ev->ev_events);
			debug("registered vevent id %i with socket %i", vev->id, vev->vsd->sd);
		}

		/* update the timeout */
		if(timeout != NULL) {
			vev->event->ev_timeout = *timeout;
			if(vev->event->ev_timeout.tv_sec > 0 || vev->event->ev_timeout.tv_usec > 0) {
				vevent_set_timer(mgr, vev, timeout);
			}
		} else {
			/* wait forever, if this is a EV_TIMEOUT event, this means
			 * the event will be considered canceled and never fire. */
			vev->timerid = -1;
			vev->event->ev_timeout.tv_sec = 0;
			vev->event->ev_timeout.tv_usec = 0;
		}

		/* success */
		return 0;
		}
	}

	return -1;
}

static gint vevent_unregister(vevent_mgr_tp mgr, event_tp ev) {
	if(ev != NULL && ev->ev_base != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {

		/* unregister vevent */
		vevent_tp vev = g_hash_table_lookup(veb->vevents_by_id, &ev->ev_timeout_pos.min_heap_idx);
		g_hash_table_remove(veb->vevents_by_id, &ev->ev_timeout_pos.min_heap_idx);
		if(vev != NULL) {
			/* make sure timers get canceled */
			vev->event->ev_flags &= ~EVLIST_INSERTED;
			vev->event = NULL;
			debug("removed vevent id %d, fd %d, type %s", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
		}

		/* unregister from socket */
		vevent_socket_tp vsd = g_hash_table_lookup(veb->sockets_by_sd, &ev->ev_fd);
		if(vsd != NULL) {
			GList* result = g_queue_find_custom(vsd->vevents, vev, (GCompareFunc)vevent_isequal_cb);
			if(result != NULL) {
                g_queue_delete_link(vsd->vevents, result);
				vevent_vepoll_action(vsd->sd, 0, ev->ev_events);
				debug("unregistered vevent id %i from socket %i", vev->id, vev->vsd->sd);
			}

			if(g_queue_get_length(vsd->vevents) <= 0) {
				g_hash_table_remove(veb->sockets_by_sd, &vsd->sd);
				vevent_destroy_socket(vsd);
				debug("stop monitoring socket %d", ev->ev_fd);
			}
		}

		if(vev != NULL && vev->ntimers <= 0) {
			vevent_destroy_vevent(vev);
		}

		/* success, even if the event didnt actually exist */
		return 0;
		}
	}

	return -1;
}

static void vevent_executeAllCallback(gpointer data, gpointer argument) {
	vevent_mgr_tp mgr = data;
	GQueue* q = argument;

	/* this is the part that needs to be wrapped in node context */
	while(g_queue_get_length(q) > 0) {
		/* execute event */
		vevent_tp vev = g_queue_pop_head(q);
		vevent_execute(mgr, vev->event);
	}
}

static void vevent_execute_callbacks(vevent_mgr_tp mgr, event_base_tp eb, gint sockd, short event_type) {
	if(eb != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
		if(veb != NULL) {
			vevent_socket_tp vsd = g_hash_table_lookup(veb->sockets_by_sd, &sockd);

			if(vsd != NULL) {
				debug("getting callbacks for type %s on fd %i", vevent_get_event_type_string(mgr, event_type), sockd);

				/* keep track of the events we need to execute */
				GQueue *to_execute = g_queue_new();
				GList *event = g_queue_peek_head_link(vsd->vevents);

				while(event != NULL) {
					vevent_tp vev = event->data;
					if(vev != NULL && vev->event != NULL) {
						/* execute if event is the correct type  */
						if(vev->event->ev_events & event_type){
							vev->event->ev_res = event_type;
							g_queue_push_tail(to_execute, vev);
						}
					}
					event = event->next;
				}

				/* now execute events.
				 *
				 * Careful!, as the execution of the event could invoke a call to try
				 * and delete the event that is currently being executed, and could
				 * free the memory storing the event. So we need
				 * to either disallow deletion of in-progress events, or remove
				 * dependence on the event before executing the callback.
				 * We currently take the second approach by creating this separate list.
				 */
				debug("executing %i events for fd %i", g_queue_get_length(to_execute), sockd);

				/* need to be in node context */
				Worker* worker = worker_getPrivate();
				Application* a = worker->cached_node->application;
				Plugin* plugin = worker_getPlugin(a->software);
				plugin_executeGeneric(plugin, a->state, vevent_executeAllCallback, mgr, to_execute);

				g_queue_free(to_execute);
			}
		}
	}
}

void vevent_notify(vevent_mgr_tp mgr, gint sockd, short event_type) {
	/* an event has occurred on sockd */
	if(mgr != NULL && mgr->event_bases != NULL) {
		GList *bases = g_queue_peek_head_link(mgr->event_bases);

		/* activate all callbacks for this sockd */
		while(bases != NULL) {
			event_base_tp eb = bases->data;
			vevent_execute_callbacks(mgr, eb, sockd, event_type);
            bases = bases->next;
		}
	}
}

static void vevent_execute(vevent_mgr_tp mgr, event_tp ev) {
	/* check cancellation */
	if(ev == NULL) {
		info("ignoring NULL event");
		return;
	}

	if(ev->ev_flags & EVLIST_INSERTED) {
		if((ev->ev_events & EV_PERSIST) != EV_PERSIST) {
			/* unregister non-persistent event */
			if(vevent_unregister(mgr, ev) != 0) {
				warning("unable to unregister uncanceled event");
			}
		}
	}

	/* execute the saved function */
	debug("++++ executing event... eventid %d, fd %d, type %s", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
//	ev->ev_flags |= EVLIST_ACTIVE;

	(ev->ev_callback)(ev->ev_fd, ev->ev_res, ev->ev_arg);

//	invalid if event was deleted during callback
//	ev->ev_flags &= ~EVLIST_ACTIVE;
	debug("---- done executing event.");
}

/* start intercepted functions */

/* event2/event.h */
event_base_tp vevent_event_base_new(vevent_mgr_tp mgr) {
	if(mgr != NULL && mgr->event_bases != NULL) {
		/* create new vevent base, store pointer to it in event_base */
		vevent_base_tp veb = calloc(1, sizeof(vevent_base_t));
		veb->nextid = 0;
		veb->vevents_by_id = g_hash_table_new(g_int_hash, g_int_equal);
		veb->sockets_by_sd = g_hash_table_new(g_int_hash, g_int_equal);

		event_base_tp eb = calloc(1, sizeof(gpointer ));
		g_queue_push_tail(mgr->event_bases, eb);
		
		vevent_mgr_track_base(mgr, eb, veb);
		return eb;
	} else {
		return NULL;
	}
}

event_base_tp vevent_event_base_new_with_config(vevent_mgr_tp mgr, const struct event_config *cfg) {
	/* just ignore the config */
    return vevent_event_base_new(mgr);
}

void vevent_event_base_free(vevent_mgr_tp mgr, event_base_tp eb) {
	if(mgr != NULL && mgr->event_bases != NULL) {
		GList *removed = g_queue_find(mgr->event_bases, eb);
		vevent_destroy_base(mgr, removed->data);
        g_queue_delete_link(mgr->event_bases, removed);
        vevent_mgr_untrack_base(mgr, eb);
	}
}

const gchar* vevent_event_base_get_method(vevent_mgr_tp mgr, const event_base_tp eb) {
	return VEVENT_METHOD;
}

void vevent_event_set_log_callback(vevent_mgr_tp mgr, event_log_cb cb) {
	/* we just automatically go through shadow logging */
	return;
}

gint vevent_event_base_loop(vevent_mgr_tp mgr, event_base_tp eb, gint flags) {
	info("vevent_event_base_loop called but will have no effect");
	return 0;
}

/* this function should only be called while plugin->isExecuting */
static void vevent_executeLoopexitCallback(gpointer data, gpointer argument) {
	vevent_mgr_tp mgr = data;
	if(mgr == NULL) {
		return;
	}

	/* we are already in node context, so no need to swap */
	(mgr->loopexit_fp)(NULL);
}

static void vevent_looexitTimerCallback(gpointer data, gpointer argument) {
	Worker* worker = worker_getPrivate();
	Application* a = worker->cached_node->application;
	Plugin* plugin = worker_getPlugin(a->software);
	plugin_executeGeneric(plugin, a->state, vevent_executeLoopexitCallback, data, argument);
}

gint vevent_event_base_loopexit(vevent_mgr_tp mgr, event_base_tp eb, const struct timeval * tv) {
	/* compute delay */
	gint delay_millis = 1;
	if(tv != NULL) {
		delay_millis = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);
		if(delay_millis <= 0) {
			delay_millis = 1;
		}
	}

	/* setup callback by creating a timer */
	if(mgr != NULL && mgr->loopexit_fp != NULL) {
		vevent_mgr_timer_create(mgr, delay_millis, &vevent_looexitTimerCallback, mgr);

		info("registered loopexit callback");
	} else {
		info("called but will have no effect");
	}
	return 0;
}

gint vevent_event_assign(vevent_mgr_tp mgr, event_tp ev, event_base_tp eb, evutil_socket_t fd, short types, event_callback_fn cb, gpointer arg) {
	if(fd == -1) {
		types |= EV_TIMEOUT;
	}

	/* must have a valid event type */
	if((types & (EV_READ|EV_WRITE|EV_SIGNAL|EV_TIMEOUT))) {
		if(ev != NULL && eb != NULL) {
			vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
			if(veb != NULL) {

			ev->ev_base = eb;
			ev->ev_fd = fd;
			ev->ev_callback = cb;
			ev->ev_arg = arg;
			ev->ev_events = types;
			ev->ev_flags = 0;
			ev->ev_res = 0;

			/* use the priority field to hold the id */
			ev->ev_timeout_pos.min_heap_idx = veb->nextid++;

			debug("assigned id %i to event with sd %i and type %s", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));

			/* success! */
			return 0;
			}
		}
	}

	/* invalid arg */
	return -1;
}

event_tp vevent_event_new(vevent_mgr_tp mgr, event_base_tp eb, evutil_socket_t fd, short types, event_callback_fn cb, gpointer arg) {
	event_tp ev = calloc(1, sizeof(event_t));

	gint result = vevent_event_assign(mgr, ev, eb, fd, types, cb, arg);

	if(result == 0) {
		return ev;
	} else {
		free(ev);
		return NULL;
	}
}

void vevent_event_free(vevent_mgr_tp mgr, event_tp ev) {
	if(ev != NULL) {
		vevent_event_del(mgr, ev);
		free(ev);
	}
}

gint vevent_event_add(vevent_mgr_tp mgr, event_tp ev, const struct timeval * timeout) {
	if(ev != NULL) {
		/* ignore signal-only events */
		if(ev->ev_events == EV_SIGNAL) {
			info("ignore signal add for event id %d, fd %d, type %s", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
			return 0;
		}

		return vevent_register(mgr, ev, timeout);
	}

	return -1;
}

gint vevent_event_del(vevent_mgr_tp mgr, event_tp ev) {
	return vevent_unregister(mgr, ev);
}

void vevent_event_active(vevent_mgr_tp mgr, event_tp ev, gint flags_for_cb, short ncalls) {
	if(ev != NULL) {
		ev->ev_res = flags_for_cb;
		for(gint i = 0; i < ncalls; i++) {
			/* XXX: fragile - we are in plugin context but this could easily break */
			vevent_execute(mgr, ev);
		}
	} else {
		warning("failed because event is NULL");
	}
}

gint vevent_event_pending(vevent_mgr_tp mgr, const event_tp ev, short types, struct timeval * tv) {
	if(ev != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {
		vevent_tp vev = g_hash_table_lookup(veb->vevents_by_id, &ev->ev_timeout_pos.min_heap_idx);

		if(vev != NULL) {
			/* event has been added, check type */
			gint flags = 0;

			if (ev->ev_flags & EVLIST_INSERTED)
				flags |= (ev->ev_events & (EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL));
			if (ev->ev_flags & EVLIST_ACTIVE)
				flags |= ev->ev_res;
			if (ev->ev_flags & EVLIST_TIMEOUT)
				flags |= EV_TIMEOUT;

			types &= (EV_TIMEOUT|EV_READ|EV_WRITE|EV_SIGNAL);

			if(tv != NULL) {
				/* TODO populate with expire time */
			}

			return (flags & types) == 0 ? 0 : 1;
		}
		}
	}

	/* we are not monitoring this event */
	return 0;
}


/* event2/dns.h */
struct evdns_base * vevent_evdns_base_new(struct event_base * event_base, gint initialize_nameservers) {
	warning("function intercepted and ignored...");
	return NULL;
}

const gchar *vevent_evdns_err_to_string(gint err) {
	warning("function intercepted and ignored...");
	return NULL;
}

gint vevent_evdns_base_count_nameservers(struct evdns_base * base) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_base_clear_nameservers_and_suspend(struct evdns_base * base) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_base_resume(struct evdns_base * base) {
	warning("function intercepted and ignored...");
	return -1;
}

struct evdns_request * vevent_evdns_base_resolve_ipv4(struct evdns_base * base, const gchar *name, gint flags, evdns_callback_type callback, gpointer ptr) {
	warning("function intercepted and ignored...");
	return NULL;
}

struct evdns_request * vevent_evdns_base_resolve_reverse(struct evdns_base * base, const struct in_addr *in, gint flags, evdns_callback_type callback, gpointer ptr) {
	warning("function intercepted and ignored...");
	return NULL;
}

struct evdns_request * vevent_evdns_base_resolve_reverse_ipv6(struct evdns_base * base, const struct in6_addr *in, gint flags, evdns_callback_type callback, gpointer ptr) {
	warning("function intercepted and ignored...");
	return NULL;
}

gint vevent_evdns_base_set_option(struct evdns_base * base, const gchar *option, const gchar *val) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_base_resolv_conf_parse(struct evdns_base * base, gint flags, const gchar *const filename) {
	warning("function intercepted and ignored...");
	return -1;
}

void vevent_evdns_base_search_clear(struct evdns_base * base) {
	warning("function intercepted and ignored...");
}

void vevent_evdns_set_log_fn(evdns_debug_log_fn_type fn) {
	warning("function intercepted and ignored...");
}

void vevent_evdns_set_random_bytes_fn(void (*fn)(gchar *, size_t)) {
	warning("function intercepted and ignored...");
}

struct evdns_server_port * vevent_evdns_add_server_port_with_base(struct event_base * base, evutil_socket_t socket, gint flags, evdns_request_callback_fn_type callback, gpointer user_data) {
	warning("function intercepted and ignored...");
	return NULL;
}

void vevent_evdns_close_server_port(struct evdns_server_port * port) {
	warning("function intercepted and ignored...");
}

gint vevent_evdns_server_request_add_reply(struct evdns_server_request * req, gint section, const gchar *name, gint type, gint dns_class, gint ttl, gint datalen, gint is_name, const gchar *data) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_server_request_add_a_reply(struct evdns_server_request * req, const gchar *name, gint n, const gpointer addrs, gint ttl) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_server_request_add_ptr_reply(struct evdns_server_request * req, struct in_addr *in, const gchar *inaddr_name, const gchar *hostname, gint ttl) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_server_request_respond(struct evdns_server_request * req, gint err) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_server_request_get_requesting_addr(struct evdns_server_request * _req, struct sockaddr *sa, gint addr_len) {
	warning("function intercepted and ignored...");
	return -1;
}


/* event2/dns_compat.h */
void vevent_evdns_shutdown(gint fail_requests) {
	warning("function intercepted and ignored...");
}

gint vevent_evdns_nameserver_ip_add(const gchar *ip_as_string) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_set_option(const gchar *option, const gchar *val, gint flags) {
	warning("function intercepted and ignored...");
	return -1;
}

gint vevent_evdns_resolv_conf_parse(gint flags, const gchar *const filename) {
	warning("function intercepted and ignored...");
	return -1;
}
