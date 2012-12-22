/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#include "shd-count-down-latch.h"

struct _CountDownLatch {
	guint initialCount;
	volatile guint count;
	GCond* waiters;
	GMutex* lock;
};

CountDownLatch* countdownlatch_new(guint count) {
	CountDownLatch* latch = g_new0(CountDownLatch, 1);
	latch->initialCount = count;
	latch->count = count;
	latch->lock = g_mutex_new();
	latch->waiters = g_cond_new();
	return latch;
}

void countdownlatch_free(CountDownLatch* latch) {
	g_assert(latch);
	g_cond_free(latch->waiters);
	g_mutex_free(latch->lock);
	g_free(latch);
}

void countdownlatch_await(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(latch->lock);
	while(latch->count > 0) {
		g_cond_wait(latch->waiters, latch->lock);
	}
	g_mutex_unlock(latch->lock);
}

void countdownlatch_countDown(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(latch->lock);
	g_assert(latch->count > 0);
	(latch->count)--;
	if(latch->count == 0) {
		g_cond_broadcast(latch->waiters);
	}
	g_mutex_unlock(latch->lock);
}

void countdownlatch_countDownAwait(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(latch->lock);
	g_assert(latch->count > 0);
	(latch->count)--;
	if(latch->count == 0) {
		g_cond_broadcast(latch->waiters);
	} else {
		g_cond_wait(latch->waiters, latch->lock);
	}
	g_mutex_unlock(latch->lock);
}

void countdownlatch_reset(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(latch->lock);
	g_assert(latch->count == 0);
	latch->count = latch->initialCount;
	g_mutex_unlock(latch->lock);
}
