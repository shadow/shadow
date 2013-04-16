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

	/* track global objects: software, cdfs, plugins */
	Registry* registry;

	/* if single threaded, use this global event priority queue. if multi-
	 * threaded, use this for non-node events */
	AsyncPriorityQueue* masterEventQueue;

	/* if multi-threaded, we use worker thread */
	CountDownLatch* processingLatch;
	CountDownLatch* barrierLatch;

	/* openssl needs us to manage locking */
	GMutex* cryptoThreadLocks;
	gint numCryptoThreadLocks;

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

	GMutex lock;
	GMutex pluginInitLock;

	gint rawFrequencyKHz;
	guint numEventsCurrentInterval;
	guint numNodesWithEventsCurrentInterval;

	/* id generation counters, must be protected for thread safety */
	volatile gint workerIDCounter;

	MAGIC_DECLARE;
};

/* holds a thread-private key that each thread references to get a private
 * instance of a worker object */
static GPrivate workerKey = G_PRIVATE_INIT(worker_free);
static GPrivate preloadKey = G_PRIVATE_INIT(g_free);

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

	/* holds all events if single-threaded, and non-node events otherwise. */
	engine->masterEventQueue =
			asyncpriorityqueue_new((GCompareDataFunc)shadowevent_compare, NULL,
			(GDestroyNotify)shadowevent_free);

	engine->registry = registry_new();
	registry_register(engine->registry, CDFS, NULL, cdf_free);
	registry_register(engine->registry, PLUGINPATHS, g_free, g_free);

	engine->minTimeJump = config->minRunAhead * SIMTIME_ONE_MILLISECOND;

	engine->internet = internetwork_new();

	g_mutex_init(&(engine->lock));
	g_mutex_init(&(engine->pluginInitLock));

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

	if(engine->masterEventQueue) {
		asyncpriorityqueue_free(engine->masterEventQueue);
	}

	registry_free(engine->registry);

	GDateTime* dt_now = g_date_time_new_now_local();
    gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S");
    message("Shadow v%s shut down cleanly at %s", SHADOW_VERSION, dt_format);
    g_date_time_unref(dt_now);
    g_free(dt_format);

	for(int i = 0; i < engine->numCryptoThreadLocks; i++) {
		g_mutex_clear(&(engine->cryptoThreadLocks[i]));
	}

	random_free(engine->random);
	g_mutex_clear(&(engine->lock));
	g_mutex_clear(&(engine->pluginInitLock));

	MAGIC_CLEAR(engine);
	shadow_engine = NULL;
	g_free(engine);
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

static gint _engine_distributeEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	GList* nodeList = internetwork_getAllNodes(engine->internet);

	/* assign nodes to the worker threads so they get processed */
	GSList* listArray[engine->config->nWorkerThreads];
	memset(listArray, 0, engine->config->nWorkerThreads * sizeof(GSList*));
	gint counter = 0;

	GList* item = g_list_first(nodeList);
	while(item) {
		Node* node = item->data;

		gint i = counter % engine->config->nWorkerThreads;
		listArray[i] = g_slist_append(listArray[i], node);

		counter++;
		item = g_list_next(item);
	}

	/* we will track when workers finish processing their nodes */
	engine->processingLatch = countdownlatch_new(engine->config->nWorkerThreads + 1);
	/* after the workers finish processing, wait for barrier update */
	engine->barrierLatch = countdownlatch_new(engine->config->nWorkerThreads + 1);

	/* start up the workers */
	GSList* workerThreads = NULL;
	for(gint i = 0; i < engine->config->nWorkerThreads; i++) {
		GString* name = g_string_new(NULL);
		g_string_printf(name, "worker-%i", (i+1));
		GThread* t = g_thread_new(name->str, (GThreadFunc)worker_run, (gpointer)listArray[i]);
		workerThreads = g_slist_append(workerThreads, t);
		g_string_free(name, TRUE);
	}

	/* process all events in the priority queue */
	while(engine->executeWindowStart < engine->endTime)
	{
		/* wait for the workers to finish processing nodes before we touch them */
		countdownlatch_countDownAwait(engine->processingLatch);

		/* we are in control now, the workers are waiting at barrierLatch */
		message("execution window [%lu--%lu] ran %u events from %u active nodes",
				engine->executeWindowStart, engine->executeWindowEnd,
				engine->numEventsCurrentInterval,
				engine->numNodesWithEventsCurrentInterval);

		/* check if we should take 1 step ahead or fast-forward our execute window.
		 * since looping through all the nodes to find the minimum event is
		 * potentially expensive, we use a heuristic of only trying to jump ahead
		 * if the last interval had only a few events in it. */
		if(engine->numEventsCurrentInterval < 10) {
			/* we had no events in that interval, lets try to fast forward */
			SimulationTime minNextEventTime = SIMTIME_INVALID;

			item = g_list_first(nodeList);
			while(item) {
				Node* node = item->data;
				EventQueue* eventq = node_getEvents(node);
				Event* nextEvent = eventqueue_peek(eventq);
				if(nextEvent && (nextEvent->time < minNextEventTime)) {
					minNextEventTime = nextEvent->time;
				}
				item = g_list_next(item);
			}

			/* fast forward to the next event */
			engine->executeWindowStart = minNextEventTime;
		} else {
			/* we still have events, lets just step one interval */
			engine->executeWindowStart = engine->executeWindowEnd;
		}

		/* make sure we dont run over the end */
		engine->executeWindowEnd = engine->executeWindowStart + engine->minTimeJump;
		if(engine->executeWindowEnd > engine->endTime) {
			engine->executeWindowEnd = engine->endTime;
		}

		/* reset for next round */
		countdownlatch_reset(engine->processingLatch);
		engine->numEventsCurrentInterval = 0;
		engine->numNodesWithEventsCurrentInterval = 0;

		/* if we are done, make sure the workers know about it */
		if(engine->executeWindowStart >= engine->endTime) {
			engine->killed = TRUE;
		}

		/* release the workers for the next round, or to exit */
		countdownlatch_countDownAwait(engine->barrierLatch);
		countdownlatch_reset(engine->barrierLatch);
	}

	/* wait for the threads to finish their cleanup */
	GSList* threadItem = workerThreads;
	while(threadItem) {
		GThread* t = threadItem->data;
		g_thread_join(t);
		g_thread_unref(t);
		threadItem = g_slist_next(threadItem);
	}
	g_slist_free(workerThreads);

	for(gint i = 0; i < engine->config->nWorkerThreads; i++) {
		g_slist_free(listArray[i]);
	}

	countdownlatch_free(engine->processingLatch);
	countdownlatch_free(engine->barrierLatch);

	/* frees the list struct we own, but not the nodes it holds (those were
	 * taken care of by the workers) */
	g_list_free(nodeList);

	return 0;
}

gint engine_run(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* dont modify internet during simulation, since its not locked for threads */
	internetwork_setReadOnly(engine->internet);

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
	g_assert(engine_getNumThreads(engine) == 1);
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

Internetwork* engine_getInternet(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->internet;
}

GPrivate* engine_getWorkerKey(Engine* engine) {
	MAGIC_ASSERT(engine);
	return &(workerKey);
}

GPrivate* engine_getPreloadKey(Engine* engine) {
	MAGIC_ASSERT(engine);
	return &(preloadKey);
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

void engine_lockPluginInit(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_lock(&(engine->pluginInitLock));
}

void engine_unlockPluginInit(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_unlock(&(engine->pluginInitLock));
}

static void _engine_lock(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_lock(&(engine->lock));
}

static void _engine_unlock(Engine* engine) {
	MAGIC_ASSERT(engine);
	g_mutex_unlock(&(engine->lock));
}

gint engine_generateWorkerID(Engine* engine) {
	MAGIC_ASSERT(engine);
	_engine_lock(engine);
	gint id = engine->workerIDCounter;
	(engine->workerIDCounter)++;
	_engine_unlock(engine);
	return id;
}

gboolean engine_handleInterruptSignal(gpointer user_data) {
	Engine* engine = user_data;
	MAGIC_ASSERT(engine);

	/* handle (SIGHUP, SIGTERM, SIGINT), shutdown cleanly */
	_engine_lock(engine);
	engine->endTime = 0;
	_engine_unlock(engine);

	/* dont remove the source */
	return FALSE;
}

void engine_notifyProcessed(Engine* engine, guint numberEventsProcessed, guint numberNodesWithEvents) {
	MAGIC_ASSERT(engine);
	_engine_lock(engine);
	engine->numEventsCurrentInterval += numberEventsProcessed;
	engine->numNodesWithEventsCurrentInterval += numberNodesWithEvents;
	_engine_unlock(engine);
	countdownlatch_countDownAwait(engine->processingLatch);
	countdownlatch_countDownAwait(engine->barrierLatch);
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

void engine_cryptoLockingFunc(Engine* engine, int mode, int n) {
/* from /usr/include/openssl/crypto.h */
#define CRYPTO_LOCK		1
#define CRYPTO_UNLOCK	2
#define CRYPTO_READ		4
#define CRYPTO_WRITE	8

	MAGIC_ASSERT(engine);
	g_assert(engine->cryptoThreadLocks);

	/* TODO may want to replace this with GRWLock when moving to GLib >= 2.32 */
	GMutex* lock = &(engine->cryptoThreadLocks[n]);
	g_assert(lock);

	if(mode & CRYPTO_LOCK) {
		g_mutex_lock(lock);
	} else {
		g_mutex_unlock(lock);
	}
}

gboolean engine_cryptoSetup(Engine* engine, gint numLocks) {
	MAGIC_ASSERT(engine);

	if(numLocks) {
		_engine_lock(engine);

		if(engine->cryptoThreadLocks) {
			g_assert(numLocks <= engine->numCryptoThreadLocks);
		} else {
			engine->numCryptoThreadLocks = numLocks;
			engine->cryptoThreadLocks = g_new0(GMutex, numLocks);
			for(int i = 0; i < engine->numCryptoThreadLocks; i++) {
				g_mutex_init(&(engine->cryptoThreadLocks[i]));
			}
		}

		_engine_unlock(engine);
	}

	return TRUE;
}
