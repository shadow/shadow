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

struct _Engine {
	/* general configuration options for the simulation */
	Configuration* config;

	/* tracks overall wall-clock runtime */
	GTimer* runTimer;

	/* global simulation time, rough approximate if multi-threaded */
	SimulationTime clock;
	/* minimum allowed time jump when sending events between nodes */
	SimulationTime minTimeJump;
	/* start of current window of execution */
	SimulationTime executeWindowStart;
	/* end of current window of execution (start + min_time_jump) */
	SimulationTime executeWindowEnd;
	/* the simulator should attempt to end immediately after this time */
	SimulationTime endTime;

	/* track nodes, networks, links, and topology */
	Internetwork* internet;

	/*
	 * track global objects: software, cdfs, plugins
	 */
	Registry* registry;

	/* if single threaded, use this global event priority queue. if multi-
	 * threaded, use this for non-node events */
	AsyncPriorityQueue* masterEventQueue;

	/* if multi-threaded, we use thread pools */
	GThreadPool* workerPool;

	/* holds a thread-private key that each thread references to get a private
	 * instance of a worker object
	 */
	GStaticPrivate workerKey;

	/*
	 * condition that signals when all node's events have been processed in a
	 * given execution interval.
	 */
	GCond* workersIdle;

	/*
	 * before signaling the engine that the workers are idle, it must be idle
	 * to accept the signal.
	 */
	GMutex* engineIdle;

	/*
	 * TRUE if the engine is no longer running events and is in cleanup mode
	 */
	gboolean killed;

	/*
	 * We will not enter plugin context when set. Used when destroying threads.
	 */
	gboolean forceShadowContext;

	/* global random source from which all node random sources originate */
	Random* random;

	GMutex* lock;

	int rawFrequencyKHz;

	/*
	 * these values are modified during simulation and must be protected so
	 * they are thread safe
	 */
	struct {
		/* number of nodes left to process in current interval */
		volatile gint nNodesToProcess;

		/* id generation counters */
		volatile gint workerIDCounter;
		volatile gint objectIDCounter;
	} protect;
	MAGIC_DECLARE;
};

Engine* engine_new(Configuration* config) {
	MAGIC_ASSERT(config);

	/* Don't do anything in this function that will cause a log message. The
	 * global engine is still NULL since we are creating it now, and logging
	 * here will cause an assertion error.
	 */

	Engine* engine = g_new0(Engine, 1);
	MAGIC_INIT(engine);

	engine->config = config;
	engine->random = random_new(config->randomSeed);
	engine->runTimer = g_timer_new();

	/* initialize the singleton-per-thread worker class */
	engine->workerKey.index = 0;

	/* holds all events if single-threaded, and non-node events otherwise. */
	engine->masterEventQueue =
			asyncpriorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL,
			(GDestroyNotify)shadowevent_free);
	engine->workersIdle = g_cond_new();
	engine->engineIdle = g_mutex_new();

	engine->registry = registry_new();
	registry_register(engine->registry, SOFTWARE, NULL, software_free);
	registry_register(engine->registry, CDFS, NULL, cdf_free);
	registry_register(engine->registry, PLUGINPATHS, g_free, g_free);

	engine->minTimeJump = config->minRunAhead * SIMTIME_ONE_MILLISECOND;

	engine->internet = internetwork_new();

	engine->lock = g_mutex_new();

	/* get the raw speed of the experiment machine */
	gchar* contents = NULL;
	gsize length = 0;
	GError* error = NULL;
	if(!g_file_get_contents(CONFIG_CPU_MAX_FREQ_FILE, &contents, &length, &error)) {
		engine->rawFrequencyKHz = 0;
	} else {
		engine->rawFrequencyKHz = (guint)atoi(contents);
	}

	return engine;
}

void engine_free(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* engine is now killed */
	engine->killed = TRUE;

	/* this launches delete on all the plugins and should be called before
	 * the engine is marked "killed" and workers are destroyed.
	 */
	internetwork_free(engine->internet);

	/* we will never execute inside the plugin again */
	engine->forceShadowContext = TRUE;

	if(engine->workerPool) {
		engine_teardownWorkerThreads(engine);
	}

	if(engine->masterEventQueue) {
		asyncpriorityqueue_free(engine->masterEventQueue);
	}

	registry_free(engine->registry);
	g_cond_free(engine->workersIdle);
	g_mutex_free(engine->engineIdle);

	GDateTime* dt_now = g_date_time_new_now_local();
	gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S:%N");
	message("clean engine shutdown at %s", dt_format);
	g_date_time_unref(dt_now);
	g_free(dt_format);

	random_free(engine->random);
	g_mutex_free(engine->lock);

	MAGIC_CLEAR(engine);
	g_free(engine);
}

void engine_setupWorkerThreads(Engine* engine, gint nWorkerThreads) {
	MAGIC_ASSERT(engine);
	if(nWorkerThreads > 0) {
		/* we need some workers, create a thread pool */
		GError *error = NULL;
		engine->workerPool = g_thread_pool_new((GFunc)worker_executeEvent, engine,
				nWorkerThreads, FALSE, &error);
		if (!engine->workerPool) {
			error("thread pool failed: %s", error->message);
			g_error_free(error);
		}

		guint interval = g_thread_pool_get_max_idle_time();
		info("Threads are stopped after %lu milliseconds", interval);
	}
}

static void _engine_joinWorkerThreads(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* wait for all workers to process their events. the last worker must
	 * wait until we are actually listening for the signal before he
	 * sends us the signal to prevent deadlock. */
	if(!g_atomic_int_dec_and_test(&(engine->protect.nNodesToProcess))) {
		while(g_atomic_int_get(&(engine->protect.nNodesToProcess)))
			g_cond_wait(engine->workersIdle, engine->engineIdle);
	}
}

void engine_teardownWorkerThreads(Engine* engine) {
	MAGIC_ASSERT(engine);
	if(engine->workerPool) {
		g_thread_pool_free(engine->workerPool, FALSE, TRUE);
		engine->workerPool = NULL;
	}
}

static gint _engine_processEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	Event* next_event = asyncpriorityqueue_peek(engine->masterEventQueue);
	if(next_event) {
		Worker* worker = worker_getPrivate();
		worker->clock_now = SIMTIME_INVALID;
		worker->clock_last = 0;
		worker->cached_engine = engine;

		/* process all events in the priority queue */
		while(next_event && (next_event->time < engine->executeWindowEnd) &&
				(next_event->time < engine->endTime))
		{
			/* get next event */
			next_event = asyncpriorityqueue_pop(engine->masterEventQueue);
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

			next_event = asyncpriorityqueue_peek(engine->masterEventQueue);
		}
	}

	return 0;
}

/*
 * check all nodes, moving events that are within the execute window
 * from their mailbox into their priority queue for execution. all
 * nodes that have executable events are placed in the thread pool and
 * processed by a worker thread.
 *
 * @warning multiple threads are running as soon as the first node is
 * pushed into the thread pool
 */
static SimulationTime _engine_syncEvents(Engine* engine, GList* nodeList) {
	/* we want to return the minimum time of all events, in case we can
	 * fast-forward the next time window */
	SimulationTime minEventTime = 0;
	gboolean isMinEventTimeInitiated = FALSE;

	/* iterate the list of nodes by stepping through the items */
	GList* item = g_list_first(nodeList);
	while(item) {
		Node* node = item->data;

		/* peek mail from mailbox to check that its in our time window */
		Event* event = node_peekMail(node);

		if(event) {
			/* the first event is used to track the min event time of all nodes */
			if(isMinEventTimeInitiated) {
				minEventTime = MIN(minEventTime, event->time);
			} else {
				minEventTime = event->time;
				isMinEventTimeInitiated = TRUE;
			}
			while(event && (event->time < engine->executeWindowEnd) &&
					(event->time < engine->endTime)) {
				g_assert(event->time >= engine->executeWindowStart);

				/* this event now becomes a task that a worker will execute */
				node_pushTask(node, node_popMail(node));

				/* get the next event, if any */
				event = node_peekMail(node);
			}
		}

		/* see if this node actually has work for a worker */
		guint numTasks = node_getNumTasks(node);
		if(numTasks > 0) {
			/* now let the worker handle all the node's events */
			g_thread_pool_push(engine->workerPool, node, NULL);

			/* we just added another node that must be processed */
			g_atomic_int_inc(&(engine->protect.nNodesToProcess));
		}

		/* get the next node, if any */
		item = g_list_next(item);
	}

	/* its ok if it wasnt initiated, b/c we have a min time jump override */
	return minEventTime;
}

static gint _engine_distributeEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	GList* nodeList = internetwork_getAllNodes(engine->internet);
	SimulationTime earliestEventTime = 0;

	/* process all events in the priority queue */
	while(engine->executeWindowStart < engine->endTime)
	{
		/* set to one, so after adding nodes we can decrement and check
		 * if all nodes are done by checking for 0 */
		g_atomic_int_set(&(engine->protect.nNodesToProcess), 1);

		/* sync up our nodes, start executing events in current window.
		 * @note other threads are awoke in this call */
		earliestEventTime = _engine_syncEvents(engine, nodeList);

		/* wait for the workers to finish running node events */
		_engine_joinWorkerThreads(engine);

		/* @note other threads are now sleeping */

		/* execute any non-node events
		 * TODO: parallelize this if it becomes a problem. for now I'm assume
		 * that we wont have enough non-node events to matter.
		 * FIXME: this doesnt make sense with our action/event layout
		 */
		_engine_processEvents(engine);

		/* finally, update the allowed event execution window.
		 * if the earliest time is before executeWindowEnd, it was just executed */
		engine->executeWindowStart = earliestEventTime > engine->executeWindowEnd ?
				earliestEventTime : engine->executeWindowEnd;
		engine->executeWindowEnd = engine->executeWindowStart + engine->minTimeJump;

//		debug("updated execution window [%lu--%lu]",
//				engine->executeWindowStart, engine->executeWindowEnd);
	}

	g_list_free(nodeList);

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
	asyncpriorityqueue_push(engine->masterEventQueue, event);
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

Internetwork* engine_getInternet(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->internet;
}

GStaticPrivate* engine_getWorkerKey(Engine* engine) {
	MAGIC_ASSERT(engine);
	return &(engine->workerKey);
}

GTimer* engine_getRunTimer(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->runTimer;
}

Configuration* engine_getConfig(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->config;
}

void engine_setKillTime(Engine* engine, SimulationTime endTime) {
	MAGIC_ASSERT(engine);
	engine->endTime = endTime;
}

gboolean engine_isKilled(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->killed;
}

gboolean engine_isForced(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->forceShadowContext;
}

gboolean engine_handleInterruptSignal(gpointer user_data) {
	Engine* engine = user_data;
	MAGIC_ASSERT(engine);

	/* handle (SIGHUP, SIGTERM, SIGINT), shutdown cleanly */
	g_mutex_lock(engine->engineIdle);
	engine->endTime = 0;
	g_mutex_unlock(engine->engineIdle);

	/* dont remove the source */
	return FALSE;
}

void _engine_lock(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_lock(engine->lock);
}

void _engine_unlock(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_unlock(engine->lock);
}

gint engine_nextRandomInt(Engine* engine) {
	MAGIC_ASSERT(engine);
	_engine_lock(engine);
	gint r = random_nextInt(engine->random);
	_engine_unlock(engine);
	return r;
}

gdouble engine_nextRandomDouble(Engine* engine) {
	MAGIC_ASSERT(engine);
	_engine_lock(engine);
	gdouble r = random_nextDouble(engine->random);
	_engine_unlock(engine);
	return r;
}

guint engine_getRawCPUFrequency(Engine* engine) {
	MAGIC_ASSERT(engine);
	_engine_lock(engine);
	guint freq = engine->rawFrequencyKHz;
	_engine_unlock(engine);
	return freq;
}
