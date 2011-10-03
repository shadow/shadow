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

Engine* engine_new(Configuration* config) {
	MAGIC_ASSERT(config);

	Engine* engine = g_new(Engine, 1);
	MAGIC_INIT(engine);

	/* initialize the singleton-per-thread worker class */
	engine->worker_key = g_private_new(worker_free);
	engine->worker_id_counter = 0;

	engine->config = config;
	engine->clock = 0;

	if(engine->config->num_threads > 1) {
		/* we need some workers, create a thread pool */
		gint num_workers = engine->config->num_threads - 1;
		GError *error = NULL;
		engine->worker_pool = g_thread_pool_new(worker_execute_event, engine, num_workers, TRUE, &error);
		if (!engine->worker_pool) {
			error("thread pool failed: %s\n", error->message);
			g_error_free(error);
		}
	} else {
		/* one thread, use simple queue, no thread pool needed */
		engine->worker_pool = NULL;
	}

	engine->master_event_queue = g_queue_new();
	engine->registry = registry_new();

	engine->end_time = SIMTIME_ONE_HOUR;
	engine->killed = 0;

	engine->min_time_jump = engine->config->min_time_jump * SIMTIME_ONE_MILLISECOND;

	return engine;
}

void engine_free(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* only free thread pool if we actually needed one */
	if(engine->worker_pool) {
		g_thread_pool_free(engine->worker_pool, FALSE, TRUE);
	}

	if(engine->master_event_queue) {
		g_queue_free(engine->master_event_queue);
	}

	registry_free(engine->registry);

	MAGIC_CLEAR(engine);
	g_free(engine);
}

static gint engine_main_single(Engine* engine) {
	MAGIC_ASSERT(engine);

	Worker* worker = worker_get();
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = 0;
	worker->cached_engine = engine;

	/* process all events in the priority queue */
	while(!engine->killed && g_queue_get_length(engine->master_event_queue) > 0)
	{
		/* get next event */
		worker->cached_event = g_queue_pop_head(engine->master_event_queue);
		MAGIC_ASSERT(worker->cached_event);

		/* ensure priority */
		worker->clock_now = worker->cached_event->time;
		g_assert(worker->clock_now >= worker->clock_last);

		event_execute(worker->cached_event);
		event_free(worker->cached_event);
		worker->cached_event = NULL;

		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;
	}

	return 0;
}


static gint engine_main_multi(Engine* engine) {
	MAGIC_ASSERT(engine);
	/* manage events coming from the event mailbox to the thread pool */

	return 0;
}

gint engine_run(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* make sure our bootstrap events are set properly */
	Worker* worker = worker_get();
	worker->clock_now = 0;
	worker->cached_engine = engine;

	/* parse user simulation script, create jobs */
	debug("parsing simulation script");

	SpinEvent* se = spin_event_new(1);
	worker_schedule_event((Event*)se, SIMTIME_ONE_SECOND);
	worker_schedule_event((Event*)killengine_event_new(), SIMTIME_ONE_HOUR);

	if(engine->config->num_threads > 1) {
		/* multi threaded, manage the other workers */
		return engine_main_multi(engine);
	} else {
		/* single threaded, we are the only worker */
		return engine_main_single(engine);
	}
}

void engine_push_event(Engine* engine, Event* event) {
	MAGIC_ASSERT(engine);
	MAGIC_ASSERT(event);
	g_queue_insert_sorted(engine->master_event_queue, event, event_compare, NULL);
}

gpointer engine_lookup(Engine* engine, EngineStorage type, gint id) {
	MAGIC_ASSERT(engine);

	/*
	 * Return the item corresponding to type and id in a thread-safe way.
	 * I believe for now no protections are necessary since our registry
	 * is read-only.
	 */
	return registry_get(engine->registry, type, &id);
}
