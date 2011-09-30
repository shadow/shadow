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

typedef struct _Engine Engine;

struct _Engine {
	Configuration* config;

	GPrivate* worker_key;
	gint worker_id_counter;
	gint node_id_counter;

	SimulationTime clock;
	GAsyncQueue* event_mailbox;

	/* if single threaded, use a simple priority queue */
	GQueue* event_priority_queue;
	/* if multi-threaded, we use a worker pool as the priority queue */
	GThreadPool* worker_pool;

	gint killed;
};

Engine* engine_new(Configuration* config);
void engine_free(Engine* engine);
gint engine_run(Engine* engine);

#endif /* SHD_ENGINE_H_ */
