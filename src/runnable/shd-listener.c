/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
	utility_assert(callback);

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
