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

#include <glib.h>
#include "shadow.h"

EventVTable spinevent_vtable = {
	(EventExecuteFunc)spinevent_execute,
	(EventFreeFunc)spinevent_free
};

SpinEvent* spinevent_new(guint seconds) {
	SpinEvent* event = g_new(SpinEvent, 1);
	event_init(&(event->super), &spinevent_vtable);

	event->spin_seconds = seconds;

	return event;
}

void spinevent_free(SpinEvent* event) {
	g_assert(event);
	g_free(event);
}

void spinevent_execute(SpinEvent* event) {
	g_assert(event);

	debug("executing spin event for %u seconds", event->spin_seconds);

	guint64 i = 1000000 * event->spin_seconds;
	while(i--) {
		continue;
	}

	worker_schedule_event((Event*)spinevent_new(event->spin_seconds), event->spin_seconds * SIMTIME_ONE_SECOND);
}
