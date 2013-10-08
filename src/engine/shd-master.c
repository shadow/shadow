/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* these are only avail in glib >= 2.30, needed for signals */
#include <glib-unix.h>

#include "shadow.h"

struct _Master {
	/* general configuration options for the simulation */
	Configuration* config;

	/* tracks overall wall-clock runtime */
	GTimer* runTimer;

	/* global random source from which all node random sources originate */
	Random* random;

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

	/* TRUE if the engine is no longer running events and is in cleanup mode */
	gboolean killed;

	/* We will not enter plugin context when set. Used when destroying threads */
	gboolean forceShadowContext;

	MAGIC_DECLARE;
};

// TODO
//static gboolean _master_handleInterruptSignal(Master* master) {
//	MAGIC_ASSERT(master);
//
//	/* handle (SIGHUP, SIGTERM, SIGINT), shutdown cleanly */
//	master->endTime = 0;
//	master->killed = TRUE;
//
//	/* dont remove the source */
//	return FALSE;
//}

Master* master_new(Configuration* config) {
	MAGIC_ASSERT(config);

	/* Don't do anything in this function that will cause a log message. The
	 * global engine is still NULL since we are creating it now, and logging
	 * here will cause an assertion error.
	 */

	Master* master = g_new0(Master, 1);
	MAGIC_INIT(master);

	master->config = config;
	master->random = random_new(config->randomSeed);
	master->runTimer = g_timer_new();

	master->minTimeJump = config->minRunAhead * SIMTIME_ONE_MILLISECOND;

    /* these are only avail in glib >= 2.30
     * setup signal handlers for gracefully handling shutdowns */
//	TODO
//	g_unix_signal_add(SIGTERM, (GSourceFunc)_master_handleInterruptSignal, master);
//	g_unix_signal_add(SIGHUP, (GSourceFunc)_master_handleInterruptSignal, master);
//	g_unix_signal_add(SIGINT, (GSourceFunc)_master_handleInterruptSignal, master);

	return master;
}

void master_free(Master* master) {
	MAGIC_ASSERT(master);

	/* engine is now killed */
	master->killed = TRUE;

	GDateTime* dt_now = g_date_time_new_now_local();
    gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S");
    message("Shadow v%s shut down cleanly at %s", SHADOW_VERSION, dt_format);
    g_date_time_unref(dt_now);
    g_free(dt_format);

	random_free(master->random);

	MAGIC_CLEAR(master);
	g_free(master);
}

void master_run(Master* master) {
	MAGIC_ASSERT(master);

	guint slaveSeed = (guint)random_nextInt(master->random);
	Slave* slave = slave_new(master, master->config, slaveSeed);

	/* hook in our logging system. stack variable used to avoid errors
	 * during cleanup below. */
	GLogLevelFlags configuredLogLevel = configuration_getLogLevel(master->config);
	g_log_set_default_handler(logging_handleLog, &(configuredLogLevel));

    GDateTime* dt_now = g_date_time_new_now_local();
    gchar* dt_format = g_date_time_format(dt_now, "%F %H:%M:%S");
    message("Shadow v%s initialized at %s using GLib v%u.%u.%u",
        SHADOW_VERSION, dt_format, (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
    g_date_time_unref(dt_now);
    g_free(dt_format);

	/* store parsed actions from each user-configured simulation script  */
	GQueue* actions = g_queue_new();
	Parser* xmlParser = parser_new();

	/* parse built-in examples, or input files */
	gboolean success = TRUE;
	if(master->config->runFileExample) {
		GString* file = example_getFileExampleContents();
		success = parser_parseContents(xmlParser, file->str, file->len, actions);
		g_string_free(file, TRUE);
	} else {
		/* parse all given input XML files */
		while(success && g_queue_get_length(master->config->inputXMLFilenames) > 0) {
			GString* filename = g_queue_pop_head(master->config->inputXMLFilenames);
			success = parser_parseFile(xmlParser, filename, actions);
		}
	}

	parser_free(xmlParser);

	/* if there was an error parsing, bounce out */
	if(success) {
		message("successfully parsed Shadow XML input!");
	} else {
		g_queue_free(actions);
		error("error parsing Shadow XML input!");
	}

	/*
	 * loop through actions that were created from parsing. this will create
	 * all the nodes, networks, applications, etc., and add an application
	 * start event for each node to bootstrap the simulation. Note that the
	 * plug-in libraries themselves are not loaded until a worker needs it,
	 * since each worker will need its own private version.
	 */
	while(g_queue_get_length(actions) > 0) {
		Action* a = g_queue_pop_head(actions);
		runnable_run(a);
		runnable_free(a);
	}
	g_queue_free(actions);

	/* start running */
	gint nWorkers = configuration_getNWorkerThreads(master->config);
	debug("starting %i-threaded engine (main + %i workers)", (nWorkers + 1), nWorkers);

	/* simulation mode depends on configured number of workers */
	if(nWorkers > 0) {
		/* multi threaded, manage the other workers */
		master->executeWindowStart = 0;
		master->executeWindowEnd = master->minTimeJump;
		slave_runParallel(slave);
	} else {
		/* single threaded, we are the only worker */
		master->executeWindowStart = 0;
		master->executeWindowEnd = G_MAXUINT64;
		slave_runSerial(slave);
	}

	debug("engine finished, cleaning up...");

	slave_free(slave);
}

//static gint _engine_processEvents(Master* engine) {
//	MAGIC_ASSERT(engine);
//
//
//
//	return 0;
//}
//
//static gint _engine_distributeEvents(Master* engine) {
//	MAGIC_ASSERT(engine);
//
//	GList* nodeList = g_hash_table_get_values(engine->hosts);
//
//	/* assign nodes to the worker threads so they get processed */
//	GSList* listArray[engine->config->nWorkerThreads];
//	memset(listArray, 0, engine->config->nWorkerThreads * sizeof(GSList*));
//	gint counter = 0;
//
//	GList* item = g_list_first(nodeList);
//	while(item) {
//		Host* node = item->data;
//
//		gint i = counter % engine->config->nWorkerThreads;
//		listArray[i] = g_slist_append(listArray[i], node);
//
//		counter++;
//		item = g_list_next(item);
//	}
//
//	/* we will track when workers finish processing their nodes */
//	engine->processingLatch = countdownlatch_new(engine->config->nWorkerThreads + 1);
//	/* after the workers finish processing, wait for barrier update */
//	engine->barrierLatch = countdownlatch_new(engine->config->nWorkerThreads + 1);
//
//	/* start up the workers */
//	GSList* workerThreads = NULL;
//	for(gint i = 0; i < engine->config->nWorkerThreads; i++) {
//		GString* name = g_string_new(NULL);
//		g_string_printf(name, "worker-%i", (i+1));
//		GThread* t = g_thread_new(name->str, (GThreadFunc)worker_run, (gpointer)listArray[i]);
//		workerThreads = g_slist_append(workerThreads, t);
//		g_string_free(name, TRUE);
//	}
//
//	message("started %i worker threads", engine->config->nWorkerThreads);
//
//	/* process all events in the priority queue */
//	while(engine->executeWindowStart < engine->endTime)
//	{
//		/* wait for the workers to finish processing nodes before we touch them */
//		countdownlatch_countDownAwait(engine->processingLatch);
//
//		/* we are in control now, the workers are waiting at barrierLatch */
//		info("execution window [%"G_GUINT64_FORMAT"--%"G_GUINT64_FORMAT"] ran %u events from %u active nodes",
//				engine->executeWindowStart, engine->executeWindowEnd,
//				engine->numEventsCurrentInterval,
//				engine->numNodesWithEventsCurrentInterval);
//
//		/* check if we should take 1 step ahead or fast-forward our execute window.
//		 * since looping through all the nodes to find the minimum event is
//		 * potentially expensive, we use a heuristic of only trying to jump ahead
//		 * if the last interval had only a few events in it. */
//		if(engine->numEventsCurrentInterval < 10) {
//			/* we had no events in that interval, lets try to fast forward */
//			SimulationTime minNextEventTime = SIMTIME_INVALID;
//
//			item = g_list_first(nodeList);
//			while(item) {
//				Host* node = item->data;
//				EventQueue* eventq = host_getEvents(node);
//				Event* nextEvent = eventqueue_peek(eventq);
//				SimulationTime nextEventTime = shadowevent_getTime(nextEvent);
//				if(nextEvent && (nextEventTime < minNextEventTime)) {
//					minNextEventTime = nextEventTime;
//				}
//				item = g_list_next(item);
//			}
//
//			/* fast forward to the next event */
//			engine->executeWindowStart = minNextEventTime;
//		} else {
//			/* we still have events, lets just step one interval */
//			engine->executeWindowStart = engine->executeWindowEnd;
//		}
//
//		/* make sure we dont run over the end */
//		engine->executeWindowEnd = engine->executeWindowStart + engine->minTimeJump;
//		if(engine->executeWindowEnd > engine->endTime) {
//			engine->executeWindowEnd = engine->endTime;
//		}
//
//		/* reset for next round */
//		countdownlatch_reset(engine->processingLatch);
//		engine->numEventsCurrentInterval = 0;
//		engine->numNodesWithEventsCurrentInterval = 0;
//
//		/* if we are done, make sure the workers know about it */
//		if(engine->executeWindowStart >= engine->endTime) {
//			engine->killed = TRUE;
//		}
//
//		/* release the workers for the next round, or to exit */
//		countdownlatch_countDownAwait(engine->barrierLatch);
//		countdownlatch_reset(engine->barrierLatch);
//	}
//
//	message("waiting for %i worker threads to finish", engine->config->nWorkerThreads);
//
//	/* wait for the threads to finish their cleanup */
//	GSList* threadItem = workerThreads;
//	while(threadItem) {
//		GThread* t = threadItem->data;
//		g_thread_join(t);
//		g_thread_unref(t);
//		threadItem = g_slist_next(threadItem);
//	}
//	g_slist_free(workerThreads);
//
//	message("%i worker threads finished", engine->config->nWorkerThreads);
//
//	for(gint i = 0; i < engine->config->nWorkerThreads; i++) {
//		g_slist_free(listArray[i]);
//	}
//
//	countdownlatch_free(engine->processingLatch);
//	countdownlatch_free(engine->barrierLatch);
//
//	/* frees the list struct we own, but not the nodes it holds (those were
//	 * taken care of by the workers) */
//	g_list_free(nodeList);
//
//	return 0;
//}

SimulationTime master_getMinTimeJump(Master* master) {
	MAGIC_ASSERT(master);
	return master->minTimeJump;
}

SimulationTime master_getExecutionBarrier(Master* master) {
	MAGIC_ASSERT(master);
	return master->executeWindowEnd;
}

GTimer* master_getRunTimer(Master* master) {
	MAGIC_ASSERT(master);
	return master->runTimer;
}

void master_setKillTime(Master* master, SimulationTime endTime) {
	MAGIC_ASSERT(master);
	master->endTime = endTime;
}

gboolean master_isKilled(Master* master) {
	MAGIC_ASSERT(master);
	return master->killed;
}

void master_setKilled(Master* master, gboolean isKilled) {
	MAGIC_ASSERT(master);
	master->killed = isKilled;
}

SimulationTime master_getExecuteWindowEnd(Master* master) {
	MAGIC_ASSERT(master);
	return master->executeWindowEnd;
}

SimulationTime master_getEndTime(Master* master) {
	MAGIC_ASSERT(master);
	return master->endTime;
}

