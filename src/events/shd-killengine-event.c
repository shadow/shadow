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

EventVTable killengine_event_vtable = {
	(EventExecuteFunc) killengine_event_execute,
	(EventFreeFunc) killengine_event_free,
	MAGIC_VALUE
};

KillEngineEvent* killengine_event_new() {
	KillEngineEvent* event = g_new(KillEngineEvent, 1);
	MAGIC_INIT(event);
	event_init(&(event->super), &killengine_event_vtable);
	return event;
}

void killengine_event_free(KillEngineEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}

void killengine_event_execute(KillEngineEvent* event) {
	MAGIC_ASSERT(event);
	g_atomic_int_inc(&(shadow_engine->killed));
}
