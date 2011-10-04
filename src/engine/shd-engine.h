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

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

typedef enum _EngineStorage EngineStorage;

enum _EngineStorage {
	NODES, NETWORKS, CDFS,
};

typedef struct _Engine Engine;

struct _Engine {
	/* general configuration options for the simulation */
	Configuration* config;

	/* global simulation time, rough approximate if multi-threaded */
	SimulationTime clock;
	/* minimum allowed time jump when sending events between nodes */
	SimulationTime min_time_jump;
	/* start of current window of execution */
	SimulationTime execute_window_start;
	/* end of current window of execution (start + min_time_jump) */
	SimulationTime execute_window_end;

	/* if single threaded, use this global event priority queue. if multi-
	 * threaded, use this for non-node events */
	GQueue* master_event_queue;

	/* if multi-threaded, we use a worker pool */
	GThreadPool* worker_pool;

	/* holds a thread-private key that each thread references to get a private
	 * instance of a worker object
	 */
	GPrivate* worker_key;

	/* id counter for worker objects */
	gint worker_id_counter;

	/* id counter for node objects */
	gint node_id_counter;

	/*
	 * Keep track of all sorts of global info: simulation nodes, networks, etc.
	 * all indexed by ID.
	 */
	Registry* registry;

	gint killed;

	MAGIC_DECLARE;
};

Engine* engine_new(Configuration* config);
void engine_free(Engine* engine);
gint engine_run(Engine* engine);
void engine_push_event(Engine* engine, Event* event);
gpointer engine_lookup(Engine* engine, EngineStorage type, gint id);

#endif /* SHD_ENGINE_H_ */
