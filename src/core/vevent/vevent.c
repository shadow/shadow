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

#include "vevent.h"
#include "vevent_mgr.h"
#include "vepoll.h"
#include "vsocket.h"
#include "log.h"
#include "list.h"
#include "hashtable.h"

/* FIXME TODO:
 * -make vevent_mgr accesses local to vevent_mgr.c
 * -refactor to better utilize shadow integration
 * -refactor should allow user to remove libevent dependency while still having vevent function for shadow */

static void vevent_execute(vevent_mgr_tp mgr, event_tp ev);
static int vevent_set_timer(vevent_mgr_tp mgr, vevent_tp vev, const struct timeval *tv);


/* start helper functions */

char* vevent_get_event_type_string(vevent_mgr_tp mgr, short event_type) {
	if(mgr != NULL && mgr->typebuf != NULL) {
		void* buffer = mgr->typebuf;
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
			list_destroy(vsd->vevents);
			vsd->vevents = NULL;
		}
		free(vsd);
	}
}

static void vevent_destroy_socket_cb(void* value, int key) {
	vevent_destroy_socket((vevent_socket_tp)value);
}

static void vevent_destroy_vevent(vevent_tp vev) {
	if(vev != NULL) {
		vev->vsd = NULL;
		free(vev);
	}
}

static void vevent_destroy_vevent_cb(void* value, int key) {
	vevent_destroy_vevent((vevent_tp)value);
}

static void vevent_vepoll_action(uint16_t sockd, uint8_t add, short ev_type) {
	/* make sure we tell vepoll our preference for event notifications when the socket/pipe is ready */
	vsocket_mgr_tp vsock_mgr = global_sim_context.current_context->vsocket_mgr;
	if(vsock_mgr != NULL) {
		vepoll_tp poll = NULL;

		poll = vpipe_get_poll(vsock_mgr->vpipe_mgr, sockd);
		if(poll == NULL) {
			vsocket_tp sock = vsocket_mgr_get_socket(global_sim_context.current_context->vsocket_mgr, sockd);
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
				hashtable_walk(veb->sockets_by_sd, &vevent_destroy_socket_cb);
				hashtable_destroy(veb->sockets_by_sd);
				veb->sockets_by_sd = NULL;
			}

			if(veb->vevents_by_id != NULL) {
				hashtable_walk(veb->vevents_by_id, &vevent_destroy_vevent_cb);
				hashtable_destroy(veb->vevents_by_id);
				veb->vevents_by_id = NULL;
			}

			free(veb);
		}

		free(eb);
	}
}

static vevent_socket_tp vevent_socket_create(int sd) {
	vevent_socket_tp vsd = malloc(sizeof(vevent_socket_t));
	vsd->vevents = list_create();
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

static void vevent_timer_cb(int timerid, void* value) {
	vevent_timer_tp vt = value;

	if(vt != NULL && vt->vev != NULL && vt->mgr != NULL) {
		vevent_tp vev = vt->vev;
		event_tp ev = vev->event;

		if(ev != NULL) {
			ev->ev_flags &= ~EVLIST_TIMEOUT;

			/* execute if this timer is still valid */
			if(vev->timerid == timerid) {
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

static int vevent_isequal_cb(void* ev1, void* ev2) {
	if(ev1 != NULL && ev2 != NULL) {
		if((((vevent_tp)ev1)->id) == (((vevent_tp)ev2)->id)) {
			return 1;
		}
	}
	return 0;
}

static int vevent_set_timer(vevent_mgr_tp mgr, vevent_tp vev, const struct timeval *tv) {
	if(tv != NULL && mgr != NULL) {
		/* evtimer called with a time - lets do a call-through to shadow timer */
		int delay_millis = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);

		/* timer create result will be the timerid or an error */
		vevent_timer_tp vt = malloc(sizeof(vevent_timer_t));
		vt->mgr = mgr;
		vt->vev = vev;

		int timer_result = vevent_mgr_timer_create(mgr, delay_millis, &vevent_timer_cb, vt);

		if(timer_result != -1){
			/* success */
			vev->timerid = timer_result;
			vev->ntimers++;
			vev->event->ev_flags |= EVLIST_TIMEOUT;
			return 0;
		} else {
			dlogf(LOG_CRIT, "vevent_set_timer: error adding timer. eventid %d, fd %d, type %s\n", vev->id, vev->event->ev_fd, vevent_get_event_type_string(mgr, vev->event->ev_events));
		}
	} else {
		dlogf(LOG_CRIT, "vevent_set_timer: timer created without specifying delay. timer event not added. event id %d, fd %d, type %s\n", vev->id, vev->event->ev_fd, vevent_get_event_type_string(mgr, vev->event->ev_events));
	}

	return -1;
}

static int vevent_register(vevent_mgr_tp mgr, event_tp ev, const struct timeval * timeout) {
	/* check if the event already is added */
	if(ev != NULL && ev->ev_base != NULL) {

		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {
			/* check for the socket */
			vevent_socket_tp vsd = hashtable_get(veb->sockets_by_sd, ev->ev_fd);
			if(vsd == NULL) {
				vsd = vevent_socket_create(ev->ev_fd);
				hashtable_set(veb->sockets_by_sd, ev->ev_fd, vsd);
				debugf("vevent_register: start monitoring socket %d\n", ev->ev_fd);
			}

			/* register as event */
			vevent_tp vev = hashtable_get(veb->vevents_by_id, ev->ev_timeout_pos.min_heap_idx);
			if(vev == NULL) {
				vev = vevent_create(ev, vsd);
				hashtable_set(veb->vevents_by_id, vev->id, vev);
				vev->event->ev_flags |= EVLIST_INSERTED;
				debugf("vevent_register: inserted vevent id %d, fd %d, type %s\n", vev->id, vev->vsd->sd, vevent_get_event_type_string(mgr, vev->event->ev_events));
			}

			/* register with socket */
			if(list_search(vsd->vevents, vev, &vevent_isequal_cb) == NULL) {
				list_push_back(vsd->vevents, vev);
				vevent_vepoll_action(vsd->sd, 1, ev->ev_events);
				debugf("vevent_register: registered vevent id %i with socket %i\n", vev->id, vev->vsd->sd);
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

static int vevent_unregister(vevent_mgr_tp mgr, event_tp ev) {
	if(ev != NULL && ev->ev_base != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {

			/* unregister vevent */
			vevent_tp vev = hashtable_remove(veb->vevents_by_id, ev->ev_timeout_pos.min_heap_idx);
			if(vev != NULL) {
				/* make sure timers get canceled */
				vev->event->ev_flags &= ~EVLIST_INSERTED;
				vev->event = NULL;
				debugf("vevent_unregister: removed vevent id %d, fd %d, type %s\n", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
			}

			/* unregister from socket */
			vevent_socket_tp vsd = hashtable_get(veb->sockets_by_sd, ev->ev_fd);
			if(vsd != NULL) {
				void* result = list_remove(vsd->vevents, vev, &vevent_isequal_cb);
				if(result != NULL) {
					vevent_vepoll_action(vsd->sd, 0, ev->ev_events);
					debugf("vevent_unregister: unregistered vevent id %i from socket %i\n", vev->id, vev->vsd->sd);
				}

				if(list_get_size(vsd->vevents) <= 0) {
					hashtable_remove(veb->sockets_by_sd, vsd->sd);
					vevent_destroy_socket(vsd);
					debugf("vevent_unregister: stop monitoring socket %d\n", ev->ev_fd);
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

static void vevent_execute_callbacks(vevent_mgr_tp mgr, event_base_tp eb, int sockd, short event_type) {
	if(eb != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
		if(veb != NULL) {
			vevent_socket_tp vsd = hashtable_get(veb->sockets_by_sd, sockd);

			if(vsd != NULL) {
				debugf("getting callbacks for type %s on fd %i\n", vevent_get_event_type_string(mgr, event_type), sockd);

				/* keep track of the events we need to execute */
				list_tp to_execute = list_create();
				list_iter_tp liter = list_iterator_create(vsd->vevents);

				while(list_iterator_hasnext(liter)) {
					vevent_tp vev = list_iterator_getnext(liter);
					if(vev != NULL && vev->event != NULL) {
						/* execute if event is the correct type  */
						if(vev->event->ev_events & event_type){
							vev->event->ev_res = event_type;
							list_push_back(to_execute, vev);
						}
					}
				}

				list_iterator_destroy(liter);

				/* now execute events.
				 *
				 * Careful!, as the execution of the event could invoke a call to try
				 * and delete the event that is currently being executed, and could
				 * free the memory storing the event. So we need
				 * to either disallow deletion of in-progress events, or remove
				 * dependence on the event before executing the callback.
				 * We currently take the second approach by creating this separate list.
				 */
				debugf("executing %i events for fd %i\n", list_get_size(to_execute), sockd);

				/* need to be in node context */

				/* XXX FIXME this should not happen here but in context.c!
				 * need to swap to node context */
				context_provider_tp provider = mgr->provider;

				/* swap out env for this provider */
				context_load(provider);
				global_sim_context.exit_usable = 1;
				if(setjmp(global_sim_context.exit_env) == 1)  /* module has been destroyed if we get here. (sim_context.current_context will be NULL) */
					return;
				else {
					/* this is the part that needs to be wrapped in node context */
					while(list_get_size(to_execute) > 0) {
						/* execute event */
						vevent_tp vev = list_pop_front(to_execute);
						vevent_execute(mgr, vev->event);
					}
				}
				/* swap back to dvn holding */
				context_save();

				list_destroy(to_execute);
			}
		}
	}
}

void vevent_notify(vevent_mgr_tp mgr, int sockd, short event_type) {
	/* an event has occurred on sockd */
	if(mgr != NULL && mgr->event_bases != NULL) {
		list_iter_tp liter = list_iterator_create(mgr->event_bases);

		/* activate all callbacks for this sockd */
		while(list_iterator_hasnext(liter)) {
			event_base_tp eb = list_iterator_getnext(liter);
			vevent_execute_callbacks(mgr, eb, sockd, event_type);
		}

		list_iterator_destroy(liter);
	}
}

static void vevent_execute(vevent_mgr_tp mgr, event_tp ev) {
	/* check cancellation */
	if(ev == NULL) {
		dlogf(LOG_MSG, "vevent_execute: ignoring NULL event\n");
		return;
	}

	if(ev->ev_flags & EVLIST_INSERTED) {
		if((ev->ev_events & EV_PERSIST) != EV_PERSIST) {
			/* unregister non-persistent event */
			if(vevent_unregister(mgr, ev) != 0) {
				dlogf(LOG_WARN, "vevent_execute: unable to unregister uncanceled event\n");
			}
		}
	}

	/* execute the saved function */
	debugf("++++ executing event... eventid %d, fd %d, type %s\n", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
//	ev->ev_flags |= EVLIST_ACTIVE;

	(ev->ev_callback)(ev->ev_fd, ev->ev_res, ev->ev_arg);

//	invalid if event was deleted during callback
//	ev->ev_flags &= ~EVLIST_ACTIVE;
	debugf("---- done executing event.\n");
}

/* start intercepted functions */

/* event2/event.h */
event_base_tp vevent_event_base_new(vevent_mgr_tp mgr) {
	if(mgr != NULL && mgr->event_bases != NULL) {
		/* create new vevent base, store pointer to it in event_base */
		vevent_base_tp veb = calloc(1, sizeof(vevent_base_t));
		veb->nextid = 0;
		veb->vevents_by_id = hashtable_create(10, 0.90);
		veb->sockets_by_sd = hashtable_create(10, 0.90);

		event_base_tp eb = calloc(1, sizeof(void*));
		list_push_back(mgr->event_bases, eb);

		vevent_mgr_track_base(mgr, eb, veb);
		return eb;
	} else {
		return NULL;
	}
}

event_base_tp vevent_event_base_new_with_config(vevent_mgr_tp mgr, const struct event_config *cfg) {
        event_base_tp base = vevent_event_base_new(mgr);
}

void vevent_event_base_free(vevent_mgr_tp mgr, event_base_tp eb) {
	if(mgr != NULL && mgr->event_bases != NULL) {
		event_base_tp removed = list_remove(mgr->event_bases, eb, NULL);
		vevent_destroy_base(mgr, removed);
		vevent_mgr_untrack_base(mgr, eb);
	}
}

const char *vevent_event_base_get_method(vevent_mgr_tp mgr, const event_base_tp eb) {
	return VEVENT_METHOD;
}

void vevent_event_set_log_callback(vevent_mgr_tp mgr, event_log_cb cb) {
	/* we just automatically go through shadow logging */
	return;
}

int vevent_event_base_loop(vevent_mgr_tp mgr, event_base_tp eb, int flags) {
	dlogf(LOG_MSG, "vevent_event_base_loop called but will have no effect\n");
	return 0;
}

void vevent_call_loopexit_fn(int tid, void* arg) {
	vevent_mgr_tp mgr = arg;
	if(mgr == NULL) {
		return;
	}

	/* we are already in node context, so no need to swap */
	(mgr->loopexit_fp)(0, NULL);
}

int vevent_event_base_loopexit(vevent_mgr_tp mgr, event_base_tp eb, const struct timeval * tv) {
	/* compute delay */
	int delay_millis = 1;
	if(tv != NULL) {
		delay_millis = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);
		if(delay_millis <= 0) {
			delay_millis = 1;
		}
	}

	/* setup callback by creating a timer */
	if(mgr != NULL && mgr->loopexit_fp != NULL) {
		vevent_mgr_timer_create(mgr, delay_millis, &vevent_call_loopexit_fn, mgr);

		dlogf(LOG_INFO, "vevent_event_base_loopexit: registered loopexit callback\n");
	} else {
		dlogf(LOG_MSG, "vevent_event_base_loopexit called but will have no effect\n");
	}
	return 0;
}

int vevent_event_assign(vevent_mgr_tp mgr, event_tp ev, event_base_tp eb, evutil_socket_t fd, short types, event_callback_fn cb, void * arg) {
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

				debugf("vevent_event_assign: assigned id %i to event with sd %i and type %s\n", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));

				/* success! */
				return 0;
			}
		}
	}

	/* invalid arg */
	return -1;
}

event_tp vevent_event_new(vevent_mgr_tp mgr, event_base_tp eb, evutil_socket_t fd, short types, event_callback_fn cb, void * arg) {
	event_tp ev = calloc(1, sizeof(event_t));

	int result = vevent_event_assign(mgr, ev, eb, fd, types, cb, arg);

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

int vevent_event_add(vevent_mgr_tp mgr, event_tp ev, const struct timeval * timeout) {
	if(ev != NULL) {
		/* ignore signal-only events */
		if(ev->ev_events == EV_SIGNAL) {
			dlogf(LOG_MSG, "ignore signal add for event id %d, fd %d, type %s\n", ev->ev_timeout_pos.min_heap_idx, ev->ev_fd, vevent_get_event_type_string(mgr, ev->ev_events));
			return 0;
		}

		return vevent_register(mgr, ev, timeout);
	}

	return -1;
}

int vevent_event_del(vevent_mgr_tp mgr, event_tp ev) {
	return vevent_unregister(mgr, ev);
}

void vevent_event_active(vevent_mgr_tp mgr, event_tp ev, int flags_for_cb, short ncalls) {
	if(ev != NULL) {
		ev->ev_res = flags_for_cb;
		for(int i = 0; i < ncalls; i++) {
			vevent_execute(mgr, ev);
		}
	} else {
		dlogf(LOG_WARN, "vevent_event_active: failed because event is NULL\n");
	}
}

int vevent_event_pending(vevent_mgr_tp mgr, const event_tp ev, short types, struct timeval * tv) {
	if(ev != NULL) {
		vevent_base_tp veb = vevent_mgr_convert_base(mgr, ev->ev_base);
		if(veb != NULL) {
			vevent_tp vev = hashtable_get(veb->vevents_by_id, ev->ev_timeout_pos.min_heap_idx);

			if(vev != NULL) {
				/* event has been added, check type */
				int flags = 0;

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
struct evdns_base * vevent_evdns_base_new(struct event_base * event_base, int initialize_nameservers) {
	dlogf(LOG_WARN, "vevent_evdns_base_new: function intercepted and ignored...\n");
	return NULL;
}

const char *vevent_evdns_err_to_string(int err) {
	dlogf(LOG_WARN, "vevent_evdns_err_to_string: function intercepted and ignored...\n");
	return NULL;
}

int vevent_evdns_base_count_nameservers(struct evdns_base * base) {
	dlogf(LOG_WARN, "vevent_evdns_base_count_nameservers: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_base_clear_nameservers_and_suspend(struct evdns_base * base) {
	dlogf(LOG_WARN, "vevent_evdns_base_clear_nameservers_and_suspend: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_base_resume(struct evdns_base * base) {
	dlogf(LOG_WARN, "vevent_evdns_base_resume: function intercepted and ignored...\n");
	return -1;
}

struct evdns_request * vevent_evdns_base_resolve_ipv4(struct evdns_base * base, const char *name, int flags, evdns_callback_type callback, void *ptr) {
	dlogf(LOG_WARN, "vevent_evdns_base_resolve_ipv4: function intercepted and ignored...\n");
	return NULL;
}

struct evdns_request * vevent_evdns_base_resolve_reverse(struct evdns_base * base, const struct in_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	dlogf(LOG_WARN, "vevent_evdns_base_resolve_reverse: function intercepted and ignored...\n");
	return NULL;
}

struct evdns_request * vevent_evdns_base_resolve_reverse_ipv6(struct evdns_base * base, const struct in6_addr *in, int flags, evdns_callback_type callback, void *ptr) {
	dlogf(LOG_WARN, "vevent_evdns_base_resolve_reverse_ipv6: function intercepted and ignored...\n");
	return NULL;
}

int vevent_evdns_base_set_option(struct evdns_base * base, const char *option, const char *val) {
	dlogf(LOG_WARN, "vevent_evdns_base_set_option: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_base_resolv_conf_parse(struct evdns_base * base, int flags, const char *const filename) {
	dlogf(LOG_WARN, "vevent_evdns_base_resolv_conf_parse: function intercepted and ignored...\n");
	return -1;
}

void vevent_evdns_base_search_clear(struct evdns_base * base) {
	dlogf(LOG_WARN, "vevent_evdns_base_search_clear: function intercepted and ignored...\n");
}

void vevent_evdns_set_log_fn(evdns_debug_log_fn_type fn) {
	dlogf(LOG_WARN, "vevent_evdns_set_log_fn: function intercepted and ignored...\n");
}

void vevent_evdns_set_random_bytes_fn(void (*fn)(char *, size_t)) {
	dlogf(LOG_WARN, "vevent_evdns_set_random_bytes_fn: function intercepted and ignored...\n");
}

struct evdns_server_port * vevent_evdns_add_server_port_with_base(struct event_base * base, evutil_socket_t socket, int flags, evdns_request_callback_fn_type callback, void *user_data) {
	dlogf(LOG_WARN, "vevent_evdns_add_server_port_with_base: function intercepted and ignored...\n");
	return NULL;
}

void vevent_evdns_close_server_port(struct evdns_server_port * port) {
	dlogf(LOG_WARN, "vevent_evdns_close_server_port: function intercepted and ignored...\n");
}

int vevent_evdns_server_request_add_reply(struct evdns_server_request * req, int section, const char *name, int type, int dns_class, int ttl, int datalen, int is_name, const char *data) {
	dlogf(LOG_WARN, "vevent_evdns_server_request_add_reply: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_server_request_add_a_reply(struct evdns_server_request * req, const char *name, int n, const void *addrs, int ttl) {
	dlogf(LOG_WARN, "vevent_evdns_server_request_add_a_reply: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_server_request_add_ptr_reply(struct evdns_server_request * req, struct in_addr *in, const char *inaddr_name, const char *hostname, int ttl) {
	dlogf(LOG_WARN, "vevent_evdns_server_request_add_ptr_reply: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_server_request_respond(struct evdns_server_request * req, int err) {
	dlogf(LOG_WARN, "vevent_evdns_server_request_respond: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_server_request_get_requesting_addr(struct evdns_server_request * _req, struct sockaddr *sa, int addr_len) {
	dlogf(LOG_WARN, "vevent_evdns_server_request_get_requesting_addr: function intercepted and ignored...\n");
	return -1;
}


/* event2/dns_compat.h */
void vevent_evdns_shutdown(int fail_requests) {
	dlogf(LOG_WARN, "vevent_evdns_shutdown: function intercepted and ignored...\n");
}

int vevent_evdns_nameserver_ip_add(const char *ip_as_string) {
	dlogf(LOG_WARN, "vevent_evdns_nameserver_ip_add: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_set_option(const char *option, const char *val, int flags) {
	dlogf(LOG_WARN, "vevent_evdns_set_option: function intercepted and ignored...\n");
	return -1;
}

int vevent_evdns_resolv_conf_parse(int flags, const char *const filename) {
	dlogf(LOG_WARN, "vevent_evdns_resolv_conf_parse: function intercepted and ignored...\n");
	return -1;
}
