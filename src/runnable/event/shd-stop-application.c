/*
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

struct _StopApplicationEvent {
	Event super;
	Application* application;
	MAGIC_DECLARE;
};

EventFunctionTable stopapplication_functions = {
	(EventRunFunc) stopapplication_run,
	(EventFreeFunc) stopapplication_free,
	MAGIC_VALUE
};

StopApplicationEvent* stopapplication_new(Application* application) {
	StopApplicationEvent* event = g_new0(StopApplicationEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &stopapplication_functions);

	event->application = application;

	return event;
}

void stopapplication_run(StopApplicationEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	node_stopApplication(node, event->application);
}

void stopapplication_free(StopApplicationEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
