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

#ifndef _ipcsync_h
#define _ipcsync_h

typedef struct ipcsync_t {
	gint semid;

	guint cnt;
	guint cnt_mutex;
	guint cnt_cond;
} ipcsync_t, * ipcsync_tp;

union ipcsync_semun {
	gint              val;    /* Value for SETVAL */
	struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
	unsigned short  *array;  /* Array for GETALL, SETALL */
	struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
};

#include <glib.h>
#include "ipcsync.h"

/**
 *
 */
ipcsync_tp ipcsync_create(gint num_mutex, gint num_cond);

void ipcsync_destroy(ipcsync_tp is) ;

void ipcsync_mutex_lock(ipcsync_tp is, gint mutex_num);
gint ipcsync_mutex_trylock(ipcsync_tp is, gint mutex_num);
void ipcsync_mutex_unlock(ipcsync_tp is, gint mutex_num);

/**
 * mutex variable must be locked prior to calling
 */
void ipcsync_cond_wait(ipcsync_tp is, gint mutex_num, gint cond_num);

/** expcets to be called in a critical section, so a mutex should be locked */
void ipcsync_cond_signal(ipcsync_tp is, gint cond_num);
void ipcsync_cond_bcast(ipcsync_tp is, gint cond_num);


#endif
