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

#include <stdlib.h>
#include <assert.h>

#include <event2/event_struct.h>
#include <event2/event.h>
#include <event2/util.h>

#include "vevent_mgr.h"
#include "vevent.h"
#include "shd-plugin.h"
#include "log.h"
#include "list.h"
#include "timer.h"
#include "sim.h"
#include "context.h"
#include "hash.h"

int vevent_mgr_timer_create(vevent_mgr_tp mgr, int milli_delay, vevent_mgr_timer_callback_fp callback_function, void * cb_arg) {
	assert(global_sim_context.sim_worker);
	return dtimer_create_timer(global_sim_context.sim_worker->timer_mgr,
			global_sim_context.sim_worker->current_time,
			mgr->provider, milli_delay, callback_function, cb_arg);
}

/* internal storage must be allocated by caller. the caller should
 * register the struct that this pointer points to with shadow. */
static void vevent_mgr_init(context_provider_tp p, vevent_mgr_tp mgr) {
	/* every node needs to init its data */
	if(mgr != NULL) {
		mgr->event_bases = list_create();
		mgr->base_conversion = hashtable_create(10, .9);
		mgr->loopexit_fp = NULL;
		mgr->provider = p;
	}
}

static void vevent_mgr_uninit(vevent_mgr_tp mgr) {
	if(mgr != NULL) {
		/* delete all event bases */
		while(list_get_size(mgr->event_bases) > 0) {
			event_base_tp eb = list_pop_front(mgr->event_bases);
			vevent_destroy_base(mgr, eb);
		}

		list_destroy(mgr->event_bases);
		hashtable_destroy(mgr->base_conversion);
	}
}

void vevent_mgr_set_loopexit_fn(vevent_mgr_tp mgr, vevent_mgr_timer_callback_fp fn) {
	if(mgr != NULL) {
		mgr->loopexit_fp = fn;
	}
}

vevent_mgr_tp vevent_mgr_create(context_provider_tp p) {
	vevent_mgr_tp mgr = calloc(1, sizeof(vevent_mgr_t));
	vevent_mgr_init(p, mgr);
	return mgr;
}

void vevent_mgr_destroy(vevent_mgr_tp mgr) {
	vevent_mgr_uninit(mgr);
	if(mgr != NULL) {
		free(mgr);
	}
}

//static void vevent_mgr_add_wakeup_cb(void* val, int key, void* param) {
//	vevent_socket_tp vsd = val;
//	list_tp wake = param;
//	if(vsd != NULL && wake != NULL) {
//		/* XXX storing ints in pointers is fragile */
//		long l = (long) vsd->sd;
//		/* our pointer will be at least 32 bits, so can hold the int sd */
//		void* p = (void*) l;
//		list_push_back(wake, p);
//	}
//}
//
//void vevent_mgr_wakeup_all(vevent_mgr_tp mgr) {
//	if(mgr != NULL) {
//		/* go through every base we know about */
//		list_iter_tp bases = list_iterator_create(mgr->event_bases);
//
//		while(list_iterator_hasnext(bases)) {
//			event_base_tp eb = list_iterator_getnext(bases);
//
//			if(eb != NULL && eb->evbase != NULL) {
//				vevent_base_tp veb = eb->evbase;
//
//				/* get a list of all the sockds */
//				list_tp wake = list_create();
//				hashtable_walk_param(veb->sockets_by_sd, &vevent_mgr_add_wakeup_cb, wake);
//
//				/* iterate the sockds and execute */
//				list_iter_tp li = list_iterator_create(wake);
//
//				while(list_iterator_hasnext(li)) {
//					long l = (long)list_iterator_getnext(li);
//					int sd = (int) l;
//					vevent_mgr_notify_can_read(mgr, sd);
//					vevent_mgr_notify_can_write(mgr, sd);
//				}
//
//				list_iterator_destroy(li);
//				list_destroy(wake);
//			}
//		}
//
//		list_iterator_destroy(bases);
//	}
//}

static void vevent_mgr_print_all_cb(void* val, int key, void* param) {
	vevent_socket_tp vsd = val;
	vevent_mgr_tp mgr = param;
	if(vsd != NULL && mgr != NULL) {
		list_iter_tp liter = list_iterator_create(vsd->vevents);

		while(list_iterator_hasnext(liter)) {
			vevent_tp vev = list_iterator_getnext(liter);
			if(vev != NULL && vev->event != NULL) {
				debugf("socket %i waiting for events %s\n", key, vevent_get_event_type_string(mgr, vev->event->ev_events));
			}
		}

		list_iterator_destroy(liter);
	}
}

void vevent_mgr_print_stat(vevent_mgr_tp mgr, uint16_t sockd) {
	if(mgr != NULL) {
		/* go through every base we know about */
		list_iter_tp bases = list_iterator_create(mgr->event_bases);

		debugf("======Printing all waiting registered events for socket %u======\n", sockd);
		while(list_iterator_hasnext(bases)) {
			event_base_tp eb = list_iterator_getnext(bases);

			vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
			if(veb != NULL) {
				vevent_socket_tp vsd = hashtable_get(veb->sockets_by_sd, (unsigned int)sockd);
				vevent_mgr_print_all_cb(vsd, sockd, mgr);
			}
		}
		debugf("======Done printing======\n");

		list_iterator_destroy(bases);
	}
}

void vevent_mgr_print_all(vevent_mgr_tp mgr) {
	if(mgr != NULL) {
		/* go through every base we know about */
		list_iter_tp bases = list_iterator_create(mgr->event_bases);

		while(list_iterator_hasnext(bases)) {
			event_base_tp eb = list_iterator_getnext(bases);

			vevent_base_tp veb = vevent_mgr_convert_base(mgr, eb);
			if(veb != NULL) {
				debugf("======Printing all waiting registered events======\n");
				hashtable_walk_param(veb->sockets_by_sd, &vevent_mgr_print_all_cb, mgr);
				debugf("======Done printing======\n");
			}
		}

		list_iterator_destroy(bases);
	}
}

void vevent_mgr_notify_can_read(vevent_mgr_tp mgr, int sockfd) {
	debugf("vevent_mgr_notify_can_read: ready to read from fd %d\n", sockfd);
	vevent_notify(mgr, sockfd, EV_READ);
}

void vevent_mgr_notify_can_write(vevent_mgr_tp mgr, int sockfd) {
	debugf("vevent_mgr_notify_can_write: ready to write to fd %d\n", sockfd);
	vevent_notify(mgr, sockfd, EV_WRITE);
}

void vevent_mgr_notify_signal_received(vevent_mgr_tp mgr, int signal) {
	debugf("vevent_mgr_notify_signal_received: received signal %d.\n", signal);
	vevent_notify(mgr, signal, EV_SIGNAL);
}

void vevent_mgr_track_base(vevent_mgr_tp mgr, event_base_tp eb, vevent_base_tp veb) {
	if(eb != NULL) {
		/* TODO can we avoid the hash? */
		unsigned int key = adler32_hash((char*)eb);
		hashtable_set(mgr->base_conversion, key, veb);
	}
}

void vevent_mgr_untrack_base(vevent_mgr_tp mgr, event_base_tp eb) {
	if(eb != NULL) {
		/* TODO can we avoid the hash? */
		unsigned int key = adler32_hash((char*)eb);
		hashtable_remove(mgr->base_conversion, key);
	}
}

vevent_base_tp vevent_mgr_convert_base(vevent_mgr_tp mgr, event_base_tp eb) {
	if(eb != NULL) {
		/* TODO can we avoid the hash? */
		unsigned int key = adler32_hash((char*)eb);
		vevent_base_tp vbase = hashtable_get(mgr->base_conversion, key);
		return vbase;
	} else {
		return NULL;
	}
}
