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

EventVTable stopevent_vtable = {
	(EventExecuteFunc)stopevent_execute,
	(EventFreeFunc)stopevent_free
};

StopEvent* stopevent_new() {
	StopEvent* event = g_new(StopEvent, 1);
	event_init(&(event->super), &stopevent_vtable);
	return event;
}

void stopevent_free(StopEvent* event) {
	g_assert(event);
	g_free(event);
}

void stopevent_execute(StopEvent* event) {
	g_assert(event);
	g_atomic_int_inc(&(shadow_engine->killed));
}
