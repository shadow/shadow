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

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include "shadow.h"

typedef struct _Worker Worker;

/* @todo: move to shd-worker.c and make this an opaque structure */
struct _Worker {
	gint thread_id;

	SimulationTime clock_now;
	SimulationTime clock_last;
	SimulationTime clock_barrier;

	Random* random;

	Engine* cached_engine;
	Plugin* cached_plugin;
	Node* cached_node;
	Event* cached_event;

	GHashTable* plugins;

	MAGIC_DECLARE;
};

/* returns the worker associated with the current thread */
Worker* worker_getPrivate();
void worker_free(gpointer data);

void worker_setKillTime(SimulationTime endTime);
Plugin* worker_getPlugin(Software* software);
Internetwork* worker_getInternet();
Configuration* worker_getConfig();
gboolean worker_isInShadowContext();

/**
 * Execute the given event. Return TRUE if the event was executed successfully,
 * or FALSE if there was an error during execution.
 *
 * Used as the callback for the main thread pool.
 */
void worker_threadPoolProcessNode(Node* node, Engine* engine);

void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id);

#endif /* SHD_WORKER_H_ */
