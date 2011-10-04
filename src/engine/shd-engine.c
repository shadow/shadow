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

	Event* next_event = g_queue_peek_head(engine->master_event_queue);

	/* process all events in the priority queue */
	while(!engine->killed &&
			next_event && next_event->time < engine->execute_window_end)
	{
		/* get next event */
		worker->cached_event = g_queue_pop_head(engine->master_event_queue);
		MAGIC_ASSERT(worker->cached_event);

		/* ensure priority */
		worker->clock_now = worker->cached_event->time;
		engine->clock = worker->clock_now;
		g_assert(worker->clock_now >= worker->clock_last);

		event_execute(worker->cached_event);
		event_free(worker->cached_event);
		worker->cached_event = NULL;

		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		next_event = g_queue_peek_head(engine->master_event_queue);
	}

	return 0;
}

static void engine_pull_executable_mail(gpointer data, gpointer user_data) {
	Node* node = data;
	Engine* engine = user_data;
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(engine);

	/* pop mail from mailbox, check that its in window, push as a task */
	NodeEvent* event = node_mail_pop(node);
	while(event && (event->super.time < engine->execute_window_end)) {
		g_assert(event->super.time >= engine->execute_window_start);
		node_task_push(node, event);
		event = node_mail_pop(node);
	}

	/* if the last event we popped was beyond the allowed execution window,
	 * push it back into mailbox so it gets executed during the next iteration
	 */
	if(event && (event->super.time >= engine->execute_window_end)) {
		node_mail_push(node, event);
	}

	/* now let the worker handle all the node's events */
	GError* pool_error;
	g_thread_pool_push(engine->worker_pool, node, &pool_error);
	if (pool_error) {
		error("thread pool push failed: %s\n", pool_error->message);
		g_error_free(pool_error);
	}

	//TODO increment an atomic int counter
}

static gint engine_main_multi(Engine* engine) {
	MAGIC_ASSERT(engine);

	Worker* worker = worker_get();
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = 0;
	worker->cached_engine = engine;

	/* process all events in the priority queue */
	while(!engine->killed)
	{
		/*
		 * check all nodes, moving events that are within the execute window
		 * from their mailbox into their priority queue for execution. all
		 * nodes that have executable events are placed in the thread pool and
		 * processed by a worker thread.
		 */
		GList* node_list = registry_get_all(engine->registry, NODES);
		/* after calling this, multiple threads are running */
		g_list_foreach(node_list, engine_pull_executable_mail, engine);

		// TODO decrement an atomic int counter here since we are done
		// adding nodes to the pool. then workers will have to check for -1 after
		// decrementing it, and signal a condition variable to wake me up

		/* wait for all workers to process their events */
		// TODO wait on condition variable

		/* execute any non-node events
		 * TODO: parallelize this if it becomes a problem. for now I assume
		 * that we wont have enough non-node events to matter.
		 */
		engine_main_single(engine);
	}

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
		engine->execute_window_start = 0;
		engine->execute_window_end = engine->min_time_jump;
		return engine_main_multi(engine);
	} else {
		/* single threaded, we are the only worker */
		engine->execute_window_start = 0;
		engine->execute_window_end = G_MAXUINT64;
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
