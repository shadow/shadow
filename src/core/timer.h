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

#ifndef _timer_h
#define _timer_h

#include <glib.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <glib-2.0/glib.h>

#include "events.h"
#include "context.h"

#include "btree.h"

#define TIMER_INVALID -1

typedef struct dtimer_timerset_t {
	btree_tp timers;
	guint cur_tid;
} dtimer_timerset_t, * dtimer_timerset_tp;

/** a full timer manager system */
typedef struct dtimer_mgr_t {
	unsigned long c_tmr_cnt;

	/** timer sets addressed by IP */
	GHashTable *timersets;

	/** events tracker */
	events_tp events;

} dtimer_mgr_t, *dtimer_mgr_tp;

/* a timer element  */
typedef struct dtimer_item_t {
	gint timer_ref;
	ptime_t expire;
	gpointer callback_arg;
	context_provider_tp context_provider;
	dtimer_ontimer_cb_fp callback;
	gchar valid;
} dtimer_item_t, *dtimer_item_tp;


dtimer_mgr_tp dtimer_create_manager(events_tp events);
void dtimer_destroy_manager(dtimer_mgr_tp mgr);
gint dtimer_create_timer( dtimer_mgr_tp mgr, ptime_t cur_time, context_provider_tp cp, gint msdelay, dtimer_ontimer_cb_fp callback, gpointer callback_arg);
void dtimer_destroy_timers(dtimer_mgr_tp mgr, context_provider_tp cp);
void dtimer_destroy_timer(dtimer_mgr_tp mgr, context_provider_tp cp, gint timer_id);
void dtimer_exec_event(dtimer_mgr_tp mgr, dtimer_item_tp event);
void dtimer_destroy_event(dtimer_item_tp event);

#endif
