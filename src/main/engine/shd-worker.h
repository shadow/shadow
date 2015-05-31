/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include "shadow.h"

typedef struct _WorkerRunData WorkerRunData;
struct _WorkerRunData {
    guint threadID;
    Scheduler* scheduler;
    gpointer userData;
};

typedef struct _Worker Worker;

DNS* worker_getDNS();
Topology* worker_getTopology();
Configuration* worker_getConfig();
void worker_setKillTime(SimulationTime endTime);
gpointer worker_run(WorkerRunData*);
void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id);
void worker_schedulePacket(Packet* packet);
gboolean worker_isAlive();
Host* worker_getCurrentHost();
Thread* worker_getActiveThread();
void worker_setActiveThread(Thread* thread);
SimulationTime worker_getCurrentTime();
guint worker_getRawCPUFrequency();
gdouble worker_nextRandomDouble();
gint worker_nextRandomInt();
guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip);
guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip);
gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID);
void worker_addHost(Host* host);
gint worker_getThreadID();
void worker_setTopology(Topology* topology);
GTimer* worker_getRunTimer();
void worker_updateMinTimeJump(gdouble minPathLatency);
void worker_setCurrentTime(SimulationTime time);
gboolean worker_isFiltered(GLogLevelFlags level);
void worker_heartbeat();

void worker_freeHosts(GList*);

void worker_storeProgram(Program* prog);
Program* worker_getProgram(GQuark pluginID);
Program* worker_getPrivateProgram(GQuark pluginID);

#endif /* SHD_WORKER_H_ */
