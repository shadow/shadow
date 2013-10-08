/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

void runnable_init(Runnable* r, RunnableFunctionTable* vtable) {
	utility_assert(r && vtable);
	MAGIC_INIT(r);
	MAGIC_INIT(vtable);
	r->vtable = vtable;
}

void runnable_run(gpointer data) {
	Runnable* r = data;
	MAGIC_ASSERT(r);
	MAGIC_ASSERT(r->vtable);
	r->vtable->run(r);
}

void runnable_free(gpointer data) {
	Runnable* r = data;
	MAGIC_ASSERT(r);
	MAGIC_ASSERT(r->vtable);
	MAGIC_CLEAR(r);
	r->vtable->free(r);
}
