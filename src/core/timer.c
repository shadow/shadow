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
#include <sys/time.h>
#include <time.h>

#include "global.h"
#include "sysconfig.h"
#include "timer.h"
#include "log.h"
#include "context.h"
#include "vsocket_mgr.h"

dtimer_mgr_tp dtimer_create_manager(events_tp events){
	dtimer_mgr_tp rval = malloc(sizeof(*rval));
	rval->c_tmr_cnt = 0;
	rval->events = events;
	rval->timersets = hashtable_create(sysconfig_get_int("dtimer_tset_hashsize"), sysconfig_get_float("dtimer_tset_hashgrowth"));
	return rval;
}

static void dtimer_free_timerset_cb(void * d, int k) {
	dtimer_timerset_tp ts = d;

	if(!ts)
		return;

	btree_destroy(ts->timers);
	ts->timers = NULL;
	free(ts);
}

void dtimer_destroy_manager(dtimer_mgr_tp mgr) {
	if(mgr == NULL)
		return;

	hashtable_walk(mgr->timersets, &dtimer_free_timerset_cb);
	hashtable_destroy(mgr->timersets);

	free(mgr);

	return;
}

int dtimer_create_timer( dtimer_mgr_tp mgr, ptime_t cur_time, context_provider_tp cp, int msdelay, dtimer_ontimer_cb_fp callback, void * callback_arg ) {
	ptime_t event_time = cur_time;
	dtimer_item_tp timer_item;
	dtimer_timerset_tp ts;

	if(!callback || !mgr || !cp || !cp->vsocket_mgr)
		return TIMER_INVALID;

	event_time += msdelay;

	ts = hashtable_get(mgr->timersets, cp->vsocket_mgr->addr);
	if(!ts) {
		ts = malloc(sizeof(*ts));
		if(!ts)
			printfault(EXIT_NOMEM, "Out of memory: dtimer_create_event");
		ts->timers = btree_create(1);
		ts->cur_tid = 1;
		hashtable_set(mgr->timersets, cp->vsocket_mgr->addr, ts);
	}

	timer_item = malloc(sizeof(*timer_item));
	if(!timer_item)
		printfault(EXIT_NOMEM, "Out of memory: dtimer_create_event");

	timer_item->callback = callback;
	timer_item->callback_arg = callback_arg;
	timer_item->expire = event_time;
	timer_item->valid = 1;
	timer_item->context_provider = cp;
	timer_item->timer_ref = ts->cur_tid++;

	events_schedule(mgr->events, event_time, timer_item, EVENTS_TYPE_DTIMER);

	return timer_item->timer_ref;
}

void dtimer_destroy_timers(dtimer_mgr_tp mgr, context_provider_tp cp) {
	dtimer_timerset_tp ts;
	dtimer_item_tp timer_item;

	ts = hashtable_get(mgr->timersets, cp->vsocket_mgr->addr);
	if(!ts)
		return;

	hashtable_remove(mgr->timersets, cp->vsocket_mgr->addr);

	for(int i=0; i<btree_get_size(ts->timers); i++) {
		timer_item = btree_get_index(ts->timers, i, NULL);
		timer_item->valid = 0;
	}

	btree_destroy(ts->timers);
	ts->timers = NULL;
	return;
}

void dtimer_destroy_timer(dtimer_mgr_tp mgr, context_provider_tp cp, int timer_id) {
	dtimer_timerset_tp ts;
	dtimer_item_tp timer_item;

	ts = hashtable_get(mgr->timersets, cp->vsocket_mgr->addr);
	if(!ts)
		return;

	timer_item = btree_get(ts->timers, timer_id);
	if(!timer_item)
		return;

	timer_item->valid = 0;

	return;
}


void dtimer_exec_event(dtimer_mgr_tp mgr, dtimer_item_tp event) {
	if(!event)
		return;

	if(event->valid)
		context_execute_dtimer_cb(event->context_provider, event->callback, event->timer_ref, event->callback_arg);

	dtimer_destroy_event(event);
}

void dtimer_destroy_event(dtimer_item_tp event) {
	free(event);
}
