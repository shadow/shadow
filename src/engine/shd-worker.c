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

static Worker* worker_new(Engine* engine) {
	Worker* worker = g_new(Worker, 1);
	MAGIC_INIT(worker);

	worker->thread_id = g_atomic_int_exchange_and_add(&(engine->worker_id_counter), 1);
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_barrier = SIMTIME_INVALID;

	return worker;
}

void worker_free(gpointer data) {
	Worker* worker = data;
	MAGIC_ASSERT(worker);
	MAGIC_CLEAR(worker);
	g_free(worker);
}

Worker* worker_get() {
	/* reference the global shadow engine */
	Engine* engine = shadow_engine;
	MAGIC_ASSERT(engine);

	/* get current thread's private worker object */
	Worker* worker = g_private_get(engine->worker_key);

	/* todo: should we use g_once here instead? */
	if(!worker) {
		worker = worker_new(engine);
		g_private_set(engine->worker_key, worker);
	}

	MAGIC_ASSERT(worker);
	return worker;
}

void worker_execute_event(gpointer data, gpointer user_data) {
	/* cast our data */
	Engine* engine = user_data;
	MAGIC_ASSERT(engine);
	Node* node = data;
	MAGIC_ASSERT(node);

	/* get current thread's private worker object */
	Worker* worker = worker_get();

	/* update cache, reset clocks */
	worker->cached_engine = engine;
	worker->cached_node = node;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_barrier = engine->execute_window_end;

	/* lock the node */
	node_lock(worker->cached_node);

	worker->cached_event = node_task_pop(worker->cached_node);

	/* process all events in the nodes local queue */
	while(!engine->killed && worker->cached_event)
	{
		MAGIC_ASSERT(worker->cached_event);

		/* make sure we don't jump backward in time */
		worker->clock_now = worker->cached_event->time;
		if(worker->clock_last != SIMTIME_INVALID) {
			g_assert(worker->clock_now >= worker->clock_last);
		}

		/* do the local task */
		event_execute(worker->cached_event);

		/* update times */
		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		/* finished event can now be destroyed */
		event_free(worker->cached_event);

		/* get the next event, or NULL will tell us to break */
		worker->cached_event = node_task_pop(worker->cached_node);
	}

	/* unlock, clear cache */
	node_unlock(worker->cached_node);
	worker->cached_engine = NULL;
	worker->cached_node = NULL;
	worker->cached_event = NULL;

	/* worker thread now returns to the pool */
}

void worker_schedule_event(Event* event, SimulationTime nano_delay) {
	MAGIC_ASSERT(event);

	/* get our thread-private worker */
	Worker* worker = worker_get();

	/* when the event will execute. this will be approximate if multi-threaded,
	 * since the master's time jumps between scheduling 'intervals'.
	 * i.e. some threads may execute events slightly after this one before
	 * this one actually gets executed by the engine. */
	event->time = worker->clock_now + nano_delay;

	/* always push to master queue since there is no node associated */
	engine_push_event(worker->cached_engine, event);
}

void worker_schedule_nodeevent(NodeEvent* event, SimulationTime nano_delay, gint receiver_node_id) {
	MAGIC_ASSERT(event);
	Event* super = &(event->super);
	MAGIC_ASSERT(super);

	/* get our thread-private worker */
	Worker* worker = worker_get();

	/* when the event will execute */
	event->super.time = worker->clock_now + nano_delay;

	/* parties involved */
	Node* sender = worker->cached_node;
	Node* receiver = engine_lookup(worker->cached_engine, NODES, receiver_node_id);

	/* single threaded mode is simpler than multi threaded */
	if(worker->cached_engine->config->num_threads > 1) {
		/* multi threaded, figure out where to push event */
		if(node_equal(receiver, sender) &&
				(event->super.time < worker->clock_barrier))
		{
			/* this is for our current node, push to its local queue. its ok if
			 * the event time inside of the min delay since its a local event */
			node_task_push(receiver, event);
		} else {
			/* this is for another node. send it as mail. make sure delay
			 * follows the configured minimum delay.
			 */
			SimulationTime min_time = worker->clock_now + worker->cached_engine->min_time_jump;
			if(event->super.time < min_time) {
				warning("Inter-node event time %lu changed to %lu due to minimum delay %lu",
						event->super.time, min_time, worker->cached_engine->min_time_jump);
				event->super.time = min_time;
			}
			node_mail_push(receiver, event);
		}
	} else {
		/* single threaded, push to master queue */
		engine_push_event(worker->cached_engine, (Event*)event);
	}
}
