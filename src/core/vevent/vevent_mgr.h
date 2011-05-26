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

#ifndef VEVENT_MGR_H_
#define VEVENT_MGR_H_

#include <glib-2.0/glib.h>

#include "context.h"

typedef void (*vevent_mgr_timer_callback_fp)(int timer_id, void* arg);

/* holds all event bases that the user creates (each holds pointer to a vevent base) */
typedef struct vevent_mgr_s {
	/* holds event_base_tp */
	GQueue *event_bases;
	vevent_mgr_timer_callback_fp loopexit_fp;
	char typebuf[80];
	context_provider_tp provider;
} vevent_mgr_t, *vevent_mgr_tp;

/* public vevent api */
//void vevent_mgr_init(vevent_mgr_tp mgr);
//void vevent_mgr_uninit(vevent_mgr_tp mgr);
vevent_mgr_tp vevent_mgr_create(context_provider_tp p);
void vevent_mgr_destroy(vevent_mgr_tp mgr);

int vevent_mgr_timer_create(vevent_mgr_tp mgr, int milli_delay, vevent_mgr_timer_callback_fp callback_function, void * cb_arg);
void vevent_mgr_set_loopexit_fn(vevent_mgr_tp mgr, vevent_mgr_timer_callback_fp fn);

//void vevent_mgr_wakeup_all(vevent_mgr_tp mgr);
void vevent_mgr_notify_can_read(vevent_mgr_tp mgr, int sockfd);
void vevent_mgr_notify_can_write(vevent_mgr_tp mgr, int sockfd);
void vevent_mgr_notify_signal_received(vevent_mgr_tp mgr, int signal);

/* mostly for debugging purposes */
void vevent_mgr_print_stat(vevent_mgr_tp mgr, uint16_t sockd);
void vevent_mgr_print_all(vevent_mgr_tp mgr);

#endif /* VEVENT_MGR_H_ */
