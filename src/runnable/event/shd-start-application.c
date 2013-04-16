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

struct _StartApplicationEvent {
	Event super;
	Application* application;
	MAGIC_DECLARE;
};

EventFunctionTable startapplication_functions = {
	(EventRunFunc) startapplication_run,
	(EventFreeFunc) startapplication_free,
	MAGIC_VALUE
};

StartApplicationEvent* startapplication_new(Application* application) {
	StartApplicationEvent* event = g_new0(StartApplicationEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &startapplication_functions);

	event->application = application;

	return event;
}

void startapplication_run(StartApplicationEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	node_startApplication(node, event->application);
}

void startapplication_free(StartApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
