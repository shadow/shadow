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

#ifndef RWLOCK_H_
#define RWLOCK_H_

#include <glib.h>
#include <stdint.h>
#include <pthread.h>

typedef struct rwlock_s {
	pthread_mutex_t mutex;
	pthread_cond_t read_condition;
	pthread_cond_t write_condition;
	guint32 valid;
	guint32 readers_active;
	guint32 readers_waiting;
	guint32 writers_active;
	guint32 writers_waiting;
} rwlock_t, *rwlock_tp;

#define RWLOCK_READY 0xBACADAEA
#define RWLOCK_SUCCESS 0
#define RWLOCK_ERROR -1

gint rwlock_init(rwlock_tp lock, gint is_process_shared);
gint rwlock_destroy(rwlock_tp lock);
gint rwlock_readlock(rwlock_tp lock);
gint rwlock_readunlock(rwlock_tp lock);
gint rwlock_writelock(rwlock_tp lock);
gint rwlock_writeunlock(rwlock_tp lock);

#endif /* RWLOCK_H_ */
