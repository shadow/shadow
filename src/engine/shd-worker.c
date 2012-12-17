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

static Worker* _worker_new(gint id) {
	Worker* worker = g_new0(Worker, 1);
	MAGIC_INIT(worker);

	worker->thread_id = id;
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_barrier = SIMTIME_INVALID;

	/* each worker needs a private copy of each plug-in library */
	worker->plugins = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, plugin_free);

	return worker;
}

void worker_free(gpointer data) {
	Worker* worker = data;
	MAGIC_ASSERT(worker);

	/* calls the destroy functions we specified in g_hash_table_new_full */
	g_hash_table_destroy(worker->plugins);

	MAGIC_CLEAR(worker);
	g_free(worker);
}

Worker* worker_getPrivate() {
	/* reference the global shadow engine */
	Engine* engine = shadow_engine;

	/* get current thread's private worker object */
	Worker* worker = g_static_private_get(engine_getWorkerKey(engine));

	/* todo: should we use g_once here instead? */
	if(!worker) {
		worker = _worker_new(engine_generateWorkerID(engine));
		g_static_private_set(engine_getWorkerKey(engine), worker, worker_free);
		gboolean* preloadIsReady = g_new(gboolean, 1);
		*preloadIsReady = TRUE;
		g_static_private_set(engine_getPreloadKey(engine), preloadIsReady, g_free);
	}

	MAGIC_ASSERT(worker);
	return worker;
}

Internetwork* worker_getInternet() {
	return engine_getInternet(shadow_engine);
}

Configuration* worker_getConfig() {
	return engine_getConfig(shadow_engine);
}

void worker_setKillTime(SimulationTime endTime) {
	engine_setKillTime(shadow_engine, endTime);
}

Plugin* worker_getPlugin(GQuark pluginID, GString* pluginPath) {
	g_assert(pluginPath);

	/* worker has a private plug-in for each plugin ID */
	Worker* worker = worker_getPrivate();
	Plugin* plugin = g_hash_table_lookup(worker->plugins, &pluginID);
	if(!plugin) {
		/* plug-in has yet to be loaded by this worker. do that now. this call
		 * will copy the plug-in library to the temporary directory, and open
		 * that so each thread can execute in its own memory space.
		 */
		plugin = plugin_new(pluginID, pluginPath);
		g_hash_table_replace(worker->plugins, plugin_getID(plugin), plugin);
	}

	debug("worker %i using plug-in at %p", worker->thread_id, plugin);

	return plugin;
}

void worker_threadPoolProcessNode(Node* node, Engine* engine) {
	/* worker comes from pool to execute node's events
	 *  get current thread's private worker object */
	Worker* worker = worker_getPrivate();

	/* update cache, reset clocks */
	worker->cached_engine = engine;
	worker->cached_node = node;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_barrier = engine_getExecutionBarrier(engine);

	/* lock the node */
	node_lock(worker->cached_node);

	EventQueue* eventq = node_getEvents(worker->cached_node);
	Event* nextEvent = eventqueue_peek(eventq);

	/* process all events in the nodes local queue */
	guint nEventsProcessed = 0;
	while(nextEvent && (nextEvent->time < worker->clock_barrier))
	{
		worker->cached_event = eventqueue_pop(eventq);
		MAGIC_ASSERT(worker->cached_event);

		/* make sure we don't jump backward in time */
		worker->clock_now = worker->cached_event->time;
		if(worker->clock_last != SIMTIME_INVALID) {
			g_assert(worker->clock_now >= worker->clock_last);
		}

		/* do the local task */
		gboolean complete = shadowevent_run(worker->cached_event);

		/* update times */
		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		/* finished event can now be destroyed */
		if(complete) {
			shadowevent_free(worker->cached_event);
			nEventsProcessed++;
		}

		/* get the next event, or NULL will tell us to break */
		nextEvent = eventqueue_peek(eventq);
	}

	/* unlock, clear cache */
	node_unlock(worker->cached_node);
	worker->cached_node = NULL;
	worker->cached_event = NULL;
	worker->cached_engine = NULL;

	engine_notifyNodeProcessed(engine, nEventsProcessed);

	/* worker thread now returns to the pool */
}

void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id) {
	/* TODO create accessors, or better yet refactor the work to event class */
	MAGIC_ASSERT(event);
	MAGIC_ASSERT((&(event->super)));

	/* get our thread-private worker */
	Worker* worker = worker_getPrivate();
	Engine* engine = worker->cached_engine;

	/* when the event will execute */
	event->time = worker->clock_now + nano_delay;

	/* parties involved. sender may be NULL, receiver may not! */
	Node* sender = worker->cached_node;
	Node* receiver = receiver_node_id == 0 ? sender : internetwork_getNode(worker_getInternet(), receiver_node_id);
	g_assert(receiver);

	/* the NodeEvent needs a pointer to the correct node */
	event->node = receiver;

	/* if we are not going to execute any more events, free it and return */
	if(engine_isKilled(engine)) {
		shadowevent_free(event);
		return;
	}

	/* engine is not killed, assert accurate worker clock */
	g_assert(worker->clock_now != SIMTIME_INVALID);

	/* non-local events must be properly delayed */
	SimulationTime jump = engine_getMinTimeJump(engine);
	if(!node_isEqual(receiver, sender)) {
		SimulationTime minTime = worker->clock_now + jump;

		/* warn and adjust time if needed */
		if(event->time < minTime) {
			debug("Inter-node event time %lu changed to %lu due to minimum delay %lu",
					event->time, minTime, jump);
			event->time = minTime;
		}
	}

	/* figure out where to push the event */
	if(engine_getNumThreads(engine) > 1) {
		/* multi-threaded, push event to receiver node */
		EventQueue* eventq = node_getEvents(receiver);
		eventqueue_push(eventq, event);
	} else {
		/* single-threaded, push to master queue */
		engine_pushEvent(engine, (Event*)event);
	}
}

gboolean worker_isInShadowContext() {
	/* this must return TRUE while destroying the thread pool to avoid
	 * calling worker_getPrivate (which messes with threads) while trying to
	 * shutdown the threads.
	 */
	if(shadow_engine && !(engine_isForced(shadow_engine))) {
		if(g_static_private_get(engine_getPreloadKey(shadow_engine))) {
			Worker* worker = worker_getPrivate();
			if(worker->cached_plugin) {
				return plugin_isShadowContext(worker->cached_plugin);
			}
		}
	}
	/* if there is no engine or cached plugin, we are definitely in Shadow context */
	return TRUE;
}
