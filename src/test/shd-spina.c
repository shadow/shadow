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

RunnableFunctionTable spina_functions = {
    (RunnableRunFunc) spina_run,
    (RunnableFreeFunc) spina_free,
    MAGIC_VALUE
};

SpinAction* spina_new(guint seconds) {
    SpinAction* action = g_new0(SpinAction, 1);
    MAGIC_INIT(action);

    action_init(&(action->super), &spina_functions);
    action->spin_seconds = seconds;

    return action;
}

void spina_free(SpinAction* action) {
    MAGIC_ASSERT(action);
    MAGIC_CLEAR(action);
    g_free(action);
}

void spina_run(SpinAction* action) {
    MAGIC_ASSERT(action);

    debug("running spin action for %u seconds", action->spin_seconds);

    guint64 i = 100000000 * action->spin_seconds;
    while(i--) {
        continue;
    }

//  SpinAction* sa = spina_new(action->spin_seconds);
//  SimulationTime t = action->spin_seconds * SIMTIME_ONE_SECOND;
//  worker_scheduleEvent((Event*)sa, t);
}
