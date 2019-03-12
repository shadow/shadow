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


Slave* slave_new(Master* master, Options* options, SimulationTime endTime, guint randomSeed);
gint slave_free(Slave* slave);

gboolean slave_isForced(Slave* slave);
guint slave_getRawCPUFrequency(Slave* slave);
guint slave_nextRandomUInt(Slave* slave);
gdouble slave_nextRandomDouble(Slave* slave);
DNS* slave_getDNS(Slave* slave);
Topology* slave_getTopology(Slave* slave);
guint32 slave_getNodeBandwidthUp(Slave* slave, GQuark nodeID, in_addr_t ip);
guint32 slave_getNodeBandwidthDown(Slave* slave, GQuark nodeID, in_addr_t ip);
gdouble slave_getLatency(Slave* slave, GQuark sourceNodeID, GQuark destinationNodeID);
Options* slave_getOptions(Slave* slave);

void slave_incrementPluginError(Slave* slave);
const gchar* slave_getHostsRootPath(Slave* slave);

void slave_updateMinTimeJump(Slave* slave, gdouble minPathLatency);

void slave_run(Slave*);
gboolean slave_schedulerIsRunning(Slave* slave);

/* info received from master to set up the simulation */
void slave_addNewProgram(Slave* slave, const gchar* name, const gchar* path, const gchar* startSymbol);
void slave_addNewVirtualHost(Slave* slave, HostParameters* params);
void slave_addNewVirtualProcess(Slave* slave, gchar* hostName, gchar* pluginName, gchar* preloadName,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void slave_addCommandToHostQueue(Slave* slave, gchar* hostName, gchar* id, SimulationTime startTime, gchar* arguments);

void slave_storeCounts(Slave* slave, ObjectCounter* objectCounter);
void slave_countObject(ObjectType otype, CounterType ctype);

#endif /* SHD_SLAVE_H_ */
