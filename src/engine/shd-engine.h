/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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

void engine_setKillTime(Engine* engine, SimulationTime endTime);
gboolean engine_isKilled(Engine* engine);
gboolean engine_isForced(Engine* engine);

void engine_setTopology(Engine* engine, Topology* top);
void engine_addHost(Engine* engine, Host* host, guint hostID);

void engine_lockPluginInit(Engine* engine);
void engine_unlockPluginInit(Engine* engine);

gpointer engine_getHost(Engine* engine, GQuark nodeID);
GList* engine_getAllHosts(Engine* engine);
guint32 engine_getNodeBandwidthUp(Engine* engine, GQuark nodeID);
guint32 engine_getNodeBandwidthDown(Engine* engine, GQuark nodeID);
gdouble engine_getLatency(Engine* engine, GQuark sourceNodeID, GQuark destinationNodeID);
DNS* engine_getDNS(Engine* engine);
Topology* engine_getTopology(Engine* engine);

/* thread-safe */

void engine_pushEvent(Engine* engine, Event* event);
gint engine_nextRandomInt(Engine* engine);
gdouble engine_nextRandomDouble(Engine* engine);
guint engine_getRawCPUFrequency(Engine* engine);

gboolean engine_cryptoSetup(Engine* engine, gint numLocks);
void engine_cryptoLockingFunc(Engine* engine, int mode, int n);

#endif /* SHD_ENGINE_H_ */
