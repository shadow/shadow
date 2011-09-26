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

static Worker* worker_new() {
	Worker* worker = g_new(Worker, 1);

	worker->thread_id = g_atomic_int_exchange_and_add(&(shadow_engine->worker_id_counter), 1);
	worker->clock_current_event = 0;
	worker->clock_last_event = 0;

	worker->event_mailbox = g_async_queue_ref(shadow_engine->event_mailbox);

	return worker;
}

void worker_free(gpointer data) {
	Worker* worker = data;
	g_assert(worker);

	g_async_queue_unref(worker->event_mailbox);
	worker->event_mailbox = NULL;
	g_free(worker);
}

Worker* worker_get() {
	/* get current thread's private worker object */
	Worker* worker = g_private_get(shadow_engine->worker_key);

	/* todo: should we use g_once here instead? */
	if(!worker) {
		worker = worker_new();
		g_private_set(shadow_engine->worker_key, worker);
	}

	return worker;
}

void worker_execute_event(gpointer data, gpointer user_data) {
	/* cast our data */
	Event* event = data;
	g_assert(event);

	/* get current thread's private worker object */
	Worker* worker = worker_get();

	/* update time */
	worker->clock_current_event = event->time;

	/* make sure we don't jump backward in time */
	g_assert(worker->clock_current_event >= worker->clock_last_event);

	/* do the job */
	event_execute(event);

	/* keep track of last executed event time */
	worker->clock_last_event = event->time;
	worker->clock_current_event = SIMTIME_INVALID;

	event_free(event);
}

void worker_schedule_event(Event* event, SimulationTime nano_delay) {
	g_assert(event);

	Worker* worker = worker_get();
	event->time = worker->clock_current_event + nano_delay;

	/* pass the engine an event and let it manage the schedules */
	g_async_queue_push(worker->event_mailbox, event);
}
