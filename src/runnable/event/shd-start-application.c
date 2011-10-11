/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include <string.h>

EventVTable startapplication_vtable = {
	(EventRunFunc) startapplication_run,
	(EventFreeFunc) startapplication_free,
	MAGIC_VALUE
};

StartApplicationEvent* startapplication_new() {
	StartApplicationEvent* event = g_new0(StartApplicationEvent, 1);
	MAGIC_INIT(event);

	event_init(&(event->super), &startapplication_vtable);

	return event;
}

void startapplication_run(StartApplicationEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	/* parse the cl_args ginto separate strings */
	GQueue *args = g_queue_new();
	gchar* result = strtok(node->application->arguments->str, " ");
	while(result != NULL) {
		g_queue_push_tail(args, result);
		result = strtok(NULL, " ");
	}

	/* setup for instantiation */
	gint argc = g_queue_get_length(args);
	gchar* argv[argc];
	gint argi = 0;
	for(argi = 0; argi < argc; argi++) {
		argv[argi] = g_queue_pop_head(args);
	}
	g_queue_free(args);

	// TODO implement

//	info("Instantiating node, ip %s, hostname %s, upstream %u KBps, downstream %u KBps\n", inet_ntoa_t(addr), hostname, KBps_up, KBps_down);

	/* call module instantiation */
//	context_execute_instantiate(context_provider, argc, argv);
}

void startapplication_free(StartApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
