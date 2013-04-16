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

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

typedef enum _EngineStorage EngineStorage;

enum _EngineStorage {
	CDFS, PLUGINPATHS
};

typedef struct _Engine Engine;

Engine* engine_new(Configuration* config);
void engine_free(Engine* engine);
void engine_setupWorkerThreads(Engine* engine, gint nWorkerThreads);
void engine_teardownWorkerThreads(Engine* engine);

gint engine_run(Engine* engine);

void engine_put(Engine* engine, EngineStorage type, GQuark* id, gpointer item);
gpointer engine_get(Engine* engine, EngineStorage type, GQuark id);

gint engine_generateWorkerID(Engine* engine);
gint engine_getNumThreads(Engine* engine);
SimulationTime engine_getMinTimeJump(Engine* engine);
SimulationTime engine_getExecutionBarrier(Engine* engine);
void engine_notifyProcessed(Engine* engine, guint numberEventsProcessed, guint numberNodesWithEvents);

Configuration* engine_getConfig(Engine* engine);
GTimer* engine_getRunTimer(Engine* engine);
GPrivate* engine_getWorkerKey(Engine* engine);
GPrivate* engine_getPreloadKey(Engine* engine);
Internetwork* engine_getInternet(Engine* engine);

void engine_setKillTime(Engine* engine, SimulationTime endTime);
gboolean engine_isKilled(Engine* engine);
gboolean engine_isForced(Engine* engine);

void engine_lockPluginInit(Engine* engine);
void engine_unlockPluginInit(Engine* engine);

/* thread-safe */

void engine_pushEvent(Engine* engine, Event* event);
gint engine_nextRandomInt(Engine* engine);
gdouble engine_nextRandomDouble(Engine* engine);
guint engine_getRawCPUFrequency(Engine* engine);

gboolean engine_cryptoSetup(Engine* engine, gint numLocks);
void engine_cryptoLockingFunc(Engine* engine, int mode, int n);

#endif /* SHD_ENGINE_H_ */
