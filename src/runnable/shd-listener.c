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

RunnableFunctionTable listener_functions = {
	(RunnableRunFunc) listener_notify,
	(RunnableFreeFunc) listener_free,
	MAGIC_VALUE
};

struct _Listener {
	Runnable super;
	CallbackFunc callback;
	gpointer data;
	gpointer callbackArgument;
	MAGIC_DECLARE;
};

Listener* listener_new(CallbackFunc callback, gpointer data, gpointer callbackArgument) {
	/* better have a non-null callback if we are going to execute it */
	g_assert(callback);

	Listener* listener = g_new0(Listener, 1);
	MAGIC_INIT(listener);

	runnable_init(&(listener->super), &listener_functions);

	listener->callback = callback;
	listener->data = data;
	listener->callbackArgument = callbackArgument;

	return listener;
}

void listener_free(gpointer data) {
	Listener* listener = data;
	MAGIC_ASSERT(listener);
	MAGIC_CLEAR(listener);
	g_free(listener);
}

void listener_notify(gpointer data) {
	Listener* listener = data;
	MAGIC_ASSERT(listener);
	listener->callback(listener->data, listener->callbackArgument);
}
