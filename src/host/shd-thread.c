/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Thread {
	//context
	MAGIC_DECLARE;
};

Thread* thread_new() {
    Thread* thread = g_new0(Thread, 1);
	MAGIC_INIT(thread);


	return thread;
}

void thread_free(Thread* thread) {
	MAGIC_ASSERT(thread);


	MAGIC_CLEAR(thread);
	g_free(thread);
}

