/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include <glib.h>

#include "shd-count-down-latch.h"

struct _CountDownLatch {
	guint initialCount;
	volatile guint count;
	GCond waiters;
	GMutex lock;
};

CountDownLatch* countdownlatch_new(guint count) {
	CountDownLatch* latch = g_new0(CountDownLatch, 1);
	latch->initialCount = count;
	latch->count = count;
	g_mutex_init(&(latch->lock));
	g_cond_init(&(latch->waiters));
	return latch;
}

void countdownlatch_free(CountDownLatch* latch) {
	g_assert(latch);
	g_cond_clear(&(latch->waiters));
	g_mutex_clear(&(latch->lock));
	g_free(latch);
}

void countdownlatch_await(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(&(latch->lock));
	while(latch->count > 0) {
		g_cond_wait(&(latch->waiters), &(latch->lock));
	}
	g_mutex_unlock(&(latch->lock));
}

void countdownlatch_countDown(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(&(latch->lock));
	g_assert(latch->count > 0);
	(latch->count)--;
	if(latch->count == 0) {
		g_cond_broadcast(&(latch->waiters));
	}
	g_mutex_unlock(&(latch->lock));
}

void countdownlatch_countDownAwait(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(&(latch->lock));
	g_assert(latch->count > 0);
	(latch->count)--;
	if(latch->count == 0) {
		g_cond_broadcast(&(latch->waiters));
	} else {
		g_cond_wait(&(latch->waiters), &(latch->lock));
	}
	g_mutex_unlock(&(latch->lock));
}

void countdownlatch_reset(CountDownLatch* latch) {
	g_assert(latch);
	g_mutex_lock(&(latch->lock));
	g_assert(latch->count == 0);
	latch->count = latch->initialCount;
	g_mutex_unlock(&(latch->lock));
}
