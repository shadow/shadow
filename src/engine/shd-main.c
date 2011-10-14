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

Engine* shadow_engine;

gint shadow_main(gint argc, gchar* argv[]) {
	g_thread_init(NULL);

	/* setup configuration - this fails and aborts if invalid */
	Configuration* config = configuration_new(argc, argv);
	if(!config) {
		/* incorrect options given */
		return -1;
	} else if(config->printSoftwareVersion) {
		g_print("Shadow v%s - (c) 2010-2011 Rob G. Jansen\nReleased under the GNU GPL, v3\n", SHADOW_VERSION);
		configuration_free(config);
		return 0;
	}

	/* allocate our driving application structure */
	shadow_engine = engine_new(config);
	/* our first call to create the worker for the main thread */
	Worker* worker = worker_getPrivate();
	/* make the engine available */
	worker->cached_engine = shadow_engine;

	/* hook in our logging system */
	g_log_set_default_handler(logging_handleLog, shadow_engine);
	debug("log system initialized");

	/* store parsed actions from each user-configured simulation script  */
	GQueue* actions = g_queue_new();
	Parser* xmlParser = parser_new();
	gboolean success = TRUE;
	while(success && g_queue_get_length(config->inputXMLFilenames) > 0) {
		GString* filename = g_queue_pop_head(config->inputXMLFilenames);
		success = parser_parse(xmlParser, filename, actions);
	}
	parser_free(xmlParser);

	/* if there was an error parsing, bounce out */
	if(!success) {
		g_queue_free(actions);
		return -1;
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
	}
	g_queue_free(actions);

	/* run the engine to drive the simulation. when this returns, we are done */
	gint n = config->nWorkerThreads;
	debug("starting %i-threaded engine (main + %i workers)", (n + 1), n);
	if(n > 0) {
		engine_setupWorkerThreads(shadow_engine, n);
	}

	/* dont modify internet during simulation, since its not locked for threads */
	shadow_engine->internet->isReadOnly = 1;
	gint retval = engine_run(shadow_engine);


	/* join thread pool. workers are auto-deleted when threads end. */
	debug("engine finished, waiting for workers...");
	if(n > 0) {
		engine_teardownWorkerThreads(shadow_engine);
	}

	/* cleanup */
	configuration_free(config);
	engine_free(shadow_engine);

	return retval;
}
