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

#ifndef SHD_SLAVE_H_
#define SHD_SLAVE_H_

typedef struct _Slave Slave;

Host* _slave_getHost(Slave* slave, GQuark hostID);
void slave_addHost(Slave* slave, Host* host, guint hostID);
Slave* slave_new(Master* master, Configuration* config, guint randomSeed);
gint slave_free(Slave* slave);
gboolean slave_isForced(Slave* slave);
guint slave_getRawCPUFrequency(Slave* slave);
gint slave_nextRandomInt(Slave* slave);
gdouble slave_nextRandomDouble(Slave* slave);
GTimer* slave_getRunTimer(Slave* slave);
void slave_updateMinTimeJump(Slave* slave, gdouble minPathLatency);
void slave_heartbeat(Slave* slave, SimulationTime simClockNow);
gint slave_generateWorkerID(Slave* slave);
DNS* slave_getDNS(Slave* slave);
Topology* slave_getTopology(Slave* slave);
void slave_setTopology(Slave* slave, Topology* topology);
guint32 slave_getNodeBandwidthUp(Slave* slave, GQuark nodeID, in_addr_t ip);
guint32 slave_getNodeBandwidthDown(Slave* slave, GQuark nodeID, in_addr_t ip);
gdouble slave_getLatency(Slave* slave, GQuark sourceNodeID, GQuark destinationNodeID);
Configuration* slave_getConfig(Slave* slave);
SimulationTime slave_getExecuteWindowEnd(Slave* slave);
SimulationTime slave_getEndTime(Slave* slave);
gboolean slave_isKilled(Slave* slave);
void slave_setKillTime(Slave* slave, SimulationTime endTime);
void slave_setKilled(Slave* slave, gboolean isKilled);
SimulationTime slave_getMinTimeJump(Slave* slave);
guint slave_getWorkerCount(Slave* slave);
SimulationTime slave_getExecutionBarrier(Slave* slave);
void slave_notifyProcessed(Slave* slave, guint numberEventsProcessed, guint numberNodesWithEvents);
void slave_runParallel(Slave* slave);
void slave_runSerial(Slave* slave);
void slave_storeProgram(Slave* slave, Program* prog);
Program* slave_getProgram(Slave* slave, GQuark pluginID);

void slave_incrementPluginError(Slave* slave);

const gchar* slave_getHostsRootPath(Slave* slave);

#endif /* SHD_SLAVE_H_ */
