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

#include "shadow.h"

void runnable_init(Runnable* r, RunnableFunctionTable* vtable) {
	g_assert(r && vtable);
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
