/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _CallbackEvent {
	Event super;

	CallbackFunc callback;
	gpointer data;
	gpointer callbackArgument;

	MAGIC_DECLARE;
};
EventFunctionTable callback_functions = {
	(EventRunFunc) callback_run,
	(EventFreeFunc) callback_free,
	MAGIC_VALUE
};

CallbackEvent* callback_new(CallbackFunc callback, gpointer data, gpointer callbackArgument) {
	/* better have a non-null callback if we are going to execute it */
	utility_assert(callback);

	CallbackEvent* event = g_new0(CallbackEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &callback_functions);

	event->callback = callback;
	event->data = data;
	event->callbackArgument = callbackArgument;

	return event;
}

void callback_run(CallbackEvent* event, Host* node) {
	MAGIC_ASSERT(event);

	event->callback(event->data, event->callbackArgument);
}

void callback_free(CallbackEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
