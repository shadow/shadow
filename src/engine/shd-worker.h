/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
	Host* cached_node;
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
