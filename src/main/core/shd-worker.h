/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_WORKER_H_
#define SHD_WORKER_H_

#include <glib.h>
#include <netinet/in.h>

#include "core/logger/shd-log-level.h"
#include "core/scheduler/shd-scheduler.h"
#include "core/support/shd-definitions.h"
#include "core/support/shd-object-counter.h"
#include "core/support/shd-options.h"
#include "core/work/shd-task.h"
#include "host/shd-host.h"
#include "routing/shd-address.h"
#include "routing/shd-dns.h"
#include "routing/shd-packet.h"
#include "routing/shd-topology.h"
#include "utility/shd-count-down-latch.h"

typedef struct _WorkerRunData WorkerRunData;
struct _WorkerRunData {
    guint threadID;
    Scheduler* scheduler;
    gpointer userData;
    CountDownLatch* notifyDoneRunning;
    CountDownLatch* notifyReadyToJoin;
    CountDownLatch* notifyJoined;
};

typedef struct _Worker Worker;

DNS* worker_getDNS();
Topology* worker_getTopology();
Options* worker_getOptions();
gpointer worker_run(WorkerRunData*);
gboolean worker_scheduleTask(Task* task, SimulationTime nanoDelay);
void worker_sendPacket(Packet* packet);
gboolean worker_isAlive();

void worker_countObject(ObjectType otype, CounterType ctype);

SimulationTime worker_getCurrentTime();
EmulatedTime worker_getEmulatedTime();

gboolean worker_isBootstrapActive();
guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip);
guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip);

gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID);
gint worker_getThreadID();
void worker_updateMinTimeJump(gdouble minPathLatency);
void worker_setCurrentTime(SimulationTime time);
gboolean worker_isFiltered(LogLevel level);

void worker_bootHosts(GQueue* hosts);
void worker_freeHosts(GQueue* hosts);

Host* worker_getActiveHost();
void worker_setActiveHost(Host* host);
Process* worker_getActiveProcess();
void worker_setActiveProcess(Process* proc);

void worker_incrementPluginError();

Address* worker_resolveIPToAddress(in_addr_t ip);
Address* worker_resolveNameToAddress(const gchar* name);

#endif /* SHD_WORKER_H_ */
