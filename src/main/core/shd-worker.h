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
Options* worker_getOptions();
gpointer worker_run(WorkerRunData*);
void worker_scheduleTask(Task* task, SimulationTime nanoDelay);
void worker_sendPacket(Packet* packet);
gboolean worker_isAlive();

SimulationTime worker_getCurrentTime();
guint worker_getRawCPUFrequency();
gdouble worker_nextRandomDouble();
gint worker_nextRandomInt();
guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip);
guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip);
gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID);
gint worker_getThreadID();
GTimer* worker_getRunTimer();
void worker_updateMinTimeJump(gdouble minPathLatency);
void worker_setCurrentTime(SimulationTime time);
gboolean worker_isFiltered(GLogLevelFlags level);

void worker_bootHosts(GList* hosts);
void worker_freeHosts(GList* hosts);

Program* worker_getProgram(GQuark pluginID);
Program* worker_getPrivateProgram(GQuark pluginID);

Host* worker_getActiveHost();
void worker_setActiveHost(Host* host);
Process* worker_getActiveProcess();
void worker_setActiveProcess(Process* proc);

void worker_incrementPluginError();

const gchar* worker_getHostsRootPath();
Address* worker_resolveIPToAddress(in_addr_t ip);
Address* worker_resolveNameToAddress(const gchar* name);

#endif /* SHD_WORKER_H_ */
