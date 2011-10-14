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

	Engine* engine = g_new0(Engine, 1);
	MAGIC_INIT(engine);

	engine->config = config;

	/* initialize the singleton-per-thread worker class */
	engine->workerKey = g_private_new(worker_free);

	if(config->nWorkerThreads > 0) {
		/* we need some workers, create a thread pool */
		GError *error = NULL;
		engine->workerPool = g_thread_pool_new(worker_executeEvent, engine,
				config->nWorkerThreads, TRUE, &error);
		if (!engine->workerPool) {
			error("thread pool failed: %s\n", error->message);
			g_error_free(error);
		}
	} else {
		/* one thread, use simple queue, no thread pool needed */
		engine->workerPool = NULL;
	}

	/* holds all events if single-threaded, and non-node events otherwise. */
	engine->masterEventQueue = g_async_queue_new_full(shadowevent_free);
	engine->workersIdle = g_cond_new();
	engine->engineIdle = g_mutex_new();

	engine->registry = registry_new();
	registry_register(engine->registry, SOFTWARE, NULL, software_free);
	registry_register(engine->registry, CDFS, NULL, cdf_free);
	registry_register(engine->registry, PLUGINPATHS, g_free, g_free);

	engine->minTimeJump = config->minTimeJump * SIMTIME_ONE_MILLISECOND;

	engine->internet = internetwork_new();

	return engine;
}

void engine_free(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* only free thread pool if we actually needed one */
	if(engine->workerPool) {
		g_thread_pool_free(engine->workerPool, FALSE, TRUE);
	}

	if(engine->masterEventQueue) {
		g_async_queue_unref(engine->masterEventQueue);
	}

	g_cond_free(engine->workersIdle);
	g_mutex_free(engine->engineIdle);

	registry_free(engine->registry);

	internetwork_free(engine->internet);

	MAGIC_CLEAR(engine);
	g_free(engine);
}

static gint _engine_processEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	Worker* worker = worker_getPrivate();
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = 0;
	worker->cached_engine = engine;

	Event* next_event = g_async_queue_try_pop(engine->masterEventQueue);

	/* process all events in the priority queue */
	while(next_event && (next_event->time < engine->executeWindowEnd) &&
			(next_event->time < engine->endTime))
	{
		/* get next event */
		worker->cached_event = next_event;
		MAGIC_ASSERT(worker->cached_event);
		worker->cached_node = next_event->node;

		/* ensure priority */
		worker->clock_now = worker->cached_event->time;
		engine->clock = worker->clock_now;
		g_assert(worker->clock_now >= worker->clock_last);


		gboolean complete = shadowevent_run(worker->cached_event);
		if(complete) {
			shadowevent_free(worker->cached_event);
		}
		worker->cached_event = NULL;
		worker->cached_node = NULL;
		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		next_event = g_async_queue_try_pop(engine->masterEventQueue);
	}

	/* push the next event in case we didnt execute it */
	if(next_event) {
		engine_pushEvent(engine, next_event);
	}

	return 0;
}

static void _engine_manageExecutableMail(gpointer data, gpointer user_data) {
	Node* node = data;
	Engine* engine = user_data;
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(engine);

	/* pop mail from mailbox, check that its in window, push as a task */
	Event* event = node_popMail(node);
	while(event && (event->time < engine->executeWindowEnd)
			&& (event->time < engine->endTime)) {
		g_assert(event->time >= engine->executeWindowStart);
		node_pushTask(node, event);
		event = node_popMail(node);
	}

	/* if the last event we popped was beyond the allowed execution window,
	 * push it back into mailbox so it gets executed during the next iteration
	 */
	if(event && (event->time >= engine->executeWindowEnd)) {
		node_pushMail(node, event);
	}

	if(node_getNumTasks(node) > 0) {
		/* now let the worker handle all the node's events */
		g_thread_pool_push(engine->workerPool, node, NULL);

		/* we just added another node that must be processed */
		g_atomic_int_inc(&(engine->protect.nNodesToProcess));
	}
}

static gint _engine_distributeEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* process all events in the priority queue */
	while(engine->executeWindowStart < engine->endTime)
	{
		/* set to one, so after adding nodes we can decrement and check
		 * if all nodes are done by checking for 0 */
		g_atomic_int_set(&(engine->protect.nNodesToProcess), 1);

		/*
		 * check all nodes, moving events that are within the execute window
		 * from their mailbox into their priority queue for execution. all
		 * nodes that have executable events are placed in the thread pool and
		 * processed by a worker thread.
		 */
		GList* node_list = internetwork_getAllNodes(engine->internet);

		/* after calling this, multiple threads are running */
		g_list_foreach(node_list, _engine_manageExecutableMail, engine);
		g_list_free(node_list);

		/* wait for all workers to process their events. the last worker must
		 * wait until we are actually listening for the signal before he
		 * sends us the signal to prevent deadlock. */
		if(!g_atomic_int_dec_and_test(&(engine->protect.nNodesToProcess))) {
			while(g_atomic_int_get(&(engine->protect.nNodesToProcess)))
				g_cond_wait(engine->workersIdle, engine->engineIdle);
		}

		/* other threads are sleeping */

		/* execute any non-node events
		 * TODO: parallelize this if it becomes a problem. for now I'm assume
		 * that we wont have enough non-node events to matter.
		 * FIXME: this doesnt make sense with actions / events
		 */
		_engine_processEvents(engine);

		/*
		 * finally, update the allowed event execution window.
		 * TODO: should be able to jump to next event time of any node
		 * in case its far in the future
		 */
		engine->executeWindowStart = engine->executeWindowEnd;
		engine->executeWindowEnd += engine->minTimeJump;
		debug("updated execution window [%lu--%lu]",
				engine->executeWindowStart, engine->executeWindowEnd);
	}

	return 0;
}

gint engine_run(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* simulation mode depends on configured number of workers */
	if(engine->config->nWorkerThreads > 0) {
		/* multi threaded, manage the other workers */
		engine->executeWindowStart = 0;
		engine->executeWindowEnd = engine->minTimeJump;
		return _engine_distributeEvents(engine);
	} else {
		/* single threaded, we are the only worker */
		engine->executeWindowStart = 0;
		engine->executeWindowEnd = G_MAXUINT64;
		return _engine_processEvents(engine);
	}
}

void engine_pushEvent(Engine* engine, Event* event) {
	MAGIC_ASSERT(engine);
	MAGIC_ASSERT(event);
	g_async_queue_push_sorted(engine->masterEventQueue, event, shadowevent_compare, NULL);
}

void engine_put(Engine* engine, EngineStorage type, GQuark* id, gpointer item) {
	MAGIC_ASSERT(engine);

	/*
	 * put the item corresponding to type and id in a thread-safe way.
	 * I believe for now no protections are necessary since our registry
	 * is filled before simulation and is read only.
	 */
	registry_put(engine->registry, type, id, item);
}

gpointer engine_get(Engine* engine, EngineStorage type, GQuark id) {
	MAGIC_ASSERT(engine);

	/*
	 * Return the item corresponding to type and id in a thread-safe way.
	 * I believe for now no protections are necessary since our registry
	 * is read-only.
	 */
	return registry_get(engine->registry, type, &id);
}

gint engine_generateWorkerID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.workerIDCounter), 1);
}

gint engine_generateObjectID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.objectIDCounter), 1);
}

gint engine_getNumThreads(Engine* engine) {
	MAGIC_ASSERT(engine);
	/* number of workers plus 1 for main thread */
	return engine->config->nWorkerThreads + 1;
}

SimulationTime engine_getMinTimeJump(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->minTimeJump;
}

SimulationTime engine_getExecutionBarrier(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->executeWindowEnd;
}

void engine_notifyNodeProcessed(Engine* engine) {
	MAGIC_ASSERT(engine);

	/*
	 * if all the nodes have been processed and the engine is done adding nodes,
	 * nNodesToProcess could be 0. if it is, get the engine lock to ensure it
	 * is listening for the signal, then signal the condition.
	 */
	if(g_atomic_int_dec_and_test(&(engine->protect.nNodesToProcess))) {
		g_mutex_lock(engine->engineIdle);
		g_cond_signal(engine->workersIdle);
		g_mutex_unlock(engine->engineIdle);
	}
}
