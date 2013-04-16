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
	Application* cached_application;
	Node* cached_node;
	Event* cached_event;

	GHashTable* plugins;

	MAGIC_DECLARE;
};

/* returns the worker associated with the current thread */
Worker* worker_getPrivate();
void worker_free(gpointer data);

gpointer worker_run(GSList* nodes);

void worker_setKillTime(SimulationTime endTime);
Plugin* worker_getPlugin(GQuark pluginID, GString* pluginPath);
Internetwork* worker_getInternet();
Configuration* worker_getConfig();
gboolean worker_isInShadowContext();

void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id);

#endif /* SHD_WORKER_H_ */
