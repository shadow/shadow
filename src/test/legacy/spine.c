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

EventFunctionTable spine_functions = {
    (EventRunFunc) spine_run,
    (EventFreeFunc) spine_free,
    MAGIC_VALUE
};

SpinEvent* spine_new(guint seconds) {
    SpinEvent* event = g_new0(SpinEvent, 1);
    MAGIC_INIT(event);

    shadowevent_init(&(event->super), &spine_functions);
    event->spin_seconds = seconds;

    return event;
}

void spine_run(SpinEvent* event, Host* node) {
    MAGIC_ASSERT(event);
    MAGIC_ASSERT(node);

    debug("executing spin event for %u seconds", event->spin_seconds);

    guint64 i = 1000000 * event->spin_seconds;
    while(i--) {
        continue;
    }

    SpinEvent* se = spine_new(event->spin_seconds);
    SimulationTime t = 1;
    worker_scheduleEvent((Event*)se, t, node->id);
    SpinEvent* se2 = spine_new(event->spin_seconds);
    worker_scheduleEvent((Event*)se2, t, 0);
}

void spine_free(SpinEvent* event) {
    MAGIC_ASSERT(event);
    MAGIC_CLEAR(event);
    g_free(event);
}
