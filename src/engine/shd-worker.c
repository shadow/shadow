/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shadow.h"

static Worker* _worker_new(Engine* engine) {
	Worker* worker = g_new0(Worker, 1);
	MAGIC_INIT(worker);

	worker->cached_engine = engine;
	worker->thread_id = engine_generateWorkerID(engine);
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
	Worker* worker = g_private_get(engine_getWorkerKey(engine));

	/* todo: should we use g_once here instead? */
	if(!worker) {
		worker = _worker_new(engine);
		g_private_replace(engine_getWorkerKey(engine), worker);
		gboolean* preloadIsReady = g_new(gboolean, 1);
		*preloadIsReady = TRUE;
		g_private_replace(engine_getPreloadKey(engine), preloadIsReady);
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

static guint _worker_processNode(Worker* worker, Node* node, SimulationTime barrier) {
	/* update cache, reset clocks */
	worker->cached_node = node;
	worker->clock_last = SIMTIME_INVALID;
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_barrier = barrier;

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

	return nEventsProcessed;
}

gpointer worker_run(GSList* nodes) {
	/* get current thread's private worker object */
	Worker* worker = worker_getPrivate();

	/* continuously run all events for this worker's assigned nodes.
	 * the simulation is done when the engine is killed. */
	while(!engine_isKilled(worker->cached_engine)) {
		SimulationTime barrier = engine_getExecutionBarrier(worker->cached_engine);
		guint nEventsProcessed = 0;
		guint nNodesWithEvents = 0;

		GSList* item = nodes;
		while(item) {
			Node* node = item->data;
			guint n = _worker_processNode(worker, node, barrier);
			nEventsProcessed += n;
			if(n > 0) {
				nNodesWithEvents++;
			}
			item = g_slist_next(item);
		}

		engine_notifyProcessed(worker->cached_engine, nEventsProcessed, nNodesWithEvents);
	}

	/* free all applications before freeing any of the nodes since freeing
	 * applications may cause close() to get called on sockets which needs
	 * other node information.
	 */
	g_slist_foreach(nodes, (GFunc) node_freeAllApplications, NULL);
	g_slist_foreach(nodes, (GFunc) node_free, NULL);

	g_thread_exit(NULL);
	return NULL;
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

	/* we MAY NOT OWN the receiver, so do not write to it! */
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
		if(g_private_get(engine_getPreloadKey(shadow_engine))) {
			Worker* worker = worker_getPrivate();
			if(worker->cached_plugin) {
				return plugin_isShadowContext(worker->cached_plugin);
			}
		}
	}
	/* if there is no engine or cached plugin, we are definitely in Shadow context */
	return TRUE;
}
