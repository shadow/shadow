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

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include <glib.h>
#include "shadow.h"

typedef struct _Worker Worker;

struct _Worker {
	gint thread_id;

	SimulationTime clock_now;
	SimulationTime clock_last;
	SimulationTime clock_barrier;

	Engine* cached_engine;
	Node* cached_node;
	Event* cached_event;

	MAGIC_DECLARE;
};

/* returns the worker associated with the current thread */
Worker* worker_get();
void worker_free(gpointer data);

/**
 * Execute the given event. Return TRUE if the event was executed successfully,
 * or FALSE if there was an error during execution.
 *
 * Used as the callback for the main thread pool.
 */
void worker_execute_event(gpointer data, gpointer user_data);

void worker_schedule_event(Event* event, gint receiver_node_id, SimulationTime nano_delay);

#endif /* SHD_WORKER_H_ */
