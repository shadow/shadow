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
gpointer worker_runParallel(WorkLoad* workload);
gpointer worker_runSerial(WorkLoad* workload);
void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id);
void worker_schedulePacket(Packet* packet);
gboolean worker_isAlive();
SimulationTime worker_getCurrentTime();
guint worker_getRawCPUFrequency();
gdouble worker_nextRandomDouble();
gint worker_nextRandomInt();
guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip);
guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip);
gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID);
void worker_addHost(Host* host, guint hostID);
gint worker_getThreadID();
void worker_setTopology(Topology* topology);
GTimer* worker_getRunTimer();
void worker_updateMinTimeJump(gdouble minPathLatency);
void worker_setCurrentTime(SimulationTime time);
gboolean worker_isFiltered(GLogLevelFlags level);
void worker_heartbeat();

void worker_storeProgram(Program* prog);
Program* worker_getProgram(GQuark pluginID);
Program* worker_getPrivateProgram(GQuark pluginID);

Host* worker_getCurrentHost();
Process* worker_getActiveProcess();
void worker_setActiveProcess(Process* proc);

void worker_incrementPluginError();

const gchar* worker_getHostsRootPath();

#endif /* SHD_WORKER_H_ */
