/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include "shadow.h"

typedef struct _Worker Worker;

typedef struct _WorkLoad WorkLoad;
struct _WorkLoad {
	/* the simulation master */
	Master* master;
	/* the slave that owns this worker */
	Slave* slave;
	/* the virtual hosts assigned to this worker */
	GList* hosts;
};

Worker* worker_new(Slave* slave);
void worker_free(Worker* worker);
DNS* worker_getDNS();
Topology* worker_getTopology();
Configuration* worker_getConfig();
void worker_setKillTime(SimulationTime endTime);
Plugin* worker_getPlugin(GQuark pluginID, GString* pluginPath);
gpointer worker_runParallel(WorkLoad* workload);
gpointer worker_runSerial(WorkLoad* workload);
void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id);
void worker_schedulePacket(Packet* packet);
gboolean worker_isAlive();
gboolean worker_isInShadowContext();
Host* worker_getCurrentHost();
Application* worker_getCurrentApplication();
void worker_setCurrentApplication(Application* application);
Plugin* worker_getCurrentPlugin();
void worker_setCurrentPlugin(Plugin* plugin);
SimulationTime worker_getCurrentTime();
guint worker_getRawCPUFrequency();
gdouble worker_nextRandomDouble();
gint worker_nextRandomInt();
void worker_lockPluginInit();
void worker_unlockPluginInit();
guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip);
guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip);
gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID);
void worker_addHost(Host* host, guint hostID);
void worker_cryptoLockingFunc(gint mode, gint n);
gboolean worker_cryptoSetup(gint numLocks);
gint worker_getThreadID();
void worker_storePluginPath(GQuark pluginID, const gchar* pluginPath);
const gchar* worker_getPluginPath(GQuark pluginID);
void worker_setTopology(Topology* topology);
GTimer* worker_getRunTimer();
void worker_setCurrentTime(SimulationTime time);
gboolean worker_isFiltered(GLogLevelFlags level);

#endif /* SHD_WORKER_H_ */
