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

#ifndef _events_h
#define _events_h

#include <glib.h>
#include "evtracker.h"

#define EVENTS_TYPE_VCI 1
#define EVENTS_TYPE_DTIMER 2
#define EVENTS_TYPE_SIMOP 3
#define EVENTS_TYPE_TICKTOCK 4

typedef struct events_t {

	evtracker_tp evtracker;

} events_t, * events_tp;

typedef struct events_eholder_t {
	gpointer d;
	gint type;
} events_eholder_t, * events_eholder_tp;

events_tp events_create (void);
void events_schedule (events_tp events, ptime_t at, gpointer data, gint type);
ptime_t events_get_next_time (events_tp events);
gpointer events_dequeue  (events_tp events, ptime_t * at, gint * type);
void events_destroy (events_tp events);


#endif
