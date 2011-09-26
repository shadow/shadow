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
	g_assert(config);

	Engine* engine = g_new(Engine, 1);

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

		/* make sure the thread pool events are sorted by priority */
		g_thread_pool_set_sort_function(engine->worker_pool, event_compare, NULL);

		engine->event_priority_queue = NULL;
	} else {
		/* one thread, use simple queue, no thread pool needed */
		engine->worker_pool = NULL;
		engine->event_priority_queue = g_queue_new();
	}

	engine->event_mailbox = g_async_queue_new_full(event_free);
	engine->killed = 0;

	return engine;
}

void engine_free(Engine* engine) {
	/* only free thread pool if we actually needed one */
	if(engine->worker_pool) {
		g_thread_pool_free(engine->worker_pool, FALSE, TRUE);
	}

	if(engine->event_priority_queue) {
		g_queue_free(engine->event_priority_queue);
	}

	g_async_queue_unref(engine->event_mailbox);

	g_free(engine);
}

static gint engine_manage(Engine* engine) {
	/* manage events coming from the event mailbox to the thread pool */

	return 0;
}

static gint engine_work(Engine* engine) {
	g_assert(engine && engine->event_mailbox && engine->event_priority_queue);

	GAsyncQueue* mb = engine->event_mailbox;
	GQueue* pq = engine->event_priority_queue;

	/* process until there are no more events in the mailbox and no events
	 * waiting to be executed in the priority queue */
	while(!engine->killed &&
			(g_async_queue_length(mb) > 0 || g_queue_get_length(pq) > 0))
	{
		/* get all events from the mailbox and prioritize them */
		while(g_async_queue_length(mb) > 0) {
			g_queue_insert_sorted(pq, g_async_queue_pop(mb), event_compare, NULL);
		}

		/* execute the next event, then loop back around to ensure priority */
		Event* event = g_queue_pop_head(pq);
		worker_execute_event(event, engine);
	}

	return 0;
}

gint engine_run(Engine* engine) {
	g_assert(engine);

	/* parse user simulation script, create jobs */
	debug("parsing simulation script");

	worker_schedule_event(spinevent_new(1), SIMTIME_ONE_SECOND);
	worker_schedule_event(stopevent_new(), SIMTIME_ONE_MINUTE);

	if(engine->config->num_threads > 1) {
		/* multi threaded, manage the other workers */
		return engine_manage(engine);
	} else {
		/* single threaded, we are the only worker */
		return engine_work(engine);
	}
}
