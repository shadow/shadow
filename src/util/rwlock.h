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

#include <stdint.h>
#include <pthread.h>

typedef struct rwlock_s {
	pthread_mutex_t mutex;
	pthread_cond_t read_condition;
	pthread_cond_t write_condition;
	uint32_t valid;
	uint32_t readers_active;
	uint32_t readers_waiting;
	uint32_t writers_active;
	uint32_t writers_waiting;
} rwlock_t, *rwlock_tp;

#define RWLOCK_READY 0xBACADAEA
#define RWLOCK_SUCCESS 0
#define RWLOCK_ERROR -1

int rwlock_init(rwlock_tp lock, int is_process_shared);
int rwlock_destroy(rwlock_tp lock);
int rwlock_readlock(rwlock_tp lock);
int rwlock_readunlock(rwlock_tp lock);
int rwlock_writelock(rwlock_tp lock);
int rwlock_writeunlock(rwlock_tp lock);

#endif /* RWLOCK_H_ */
