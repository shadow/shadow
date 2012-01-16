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
