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

#ifndef SHD_MANAGER_H_
#define SHD_MANAGER_H_

#include <glib.h>
#include <netinet/in.h>

#include "main/core/controller.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/support/options.h"
#include "main/host/host.h"
#include "main/routing/dns.h"
#include "main/routing/topology.h"

typedef struct _Manager Manager;


Manager* manager_new(Master* master, Options* options, SimulationTime endTime, SimulationTime bootstrapEndTime,
        guint randomSeed);
gint manager_free(Manager* manager);

gboolean manager_isForced(Manager* manager);
guint manager_getRawCPUFrequency(Manager* manager);
DNS* manager_getDNS(Manager* manager);
Topology* manager_getTopology(Manager* manager);
guint32 manager_getNodeBandwidthUp(Manager* manager, GQuark nodeID, in_addr_t ip);
guint32 manager_getNodeBandwidthDown(Manager* manager, GQuark nodeID, in_addr_t ip);
gdouble manager_getLatency(Manager* manager, GQuark sourceNodeID, GQuark destinationNodeID);
Options* manager_getOptions(Manager* manager);
SimulationTime manager_getBootstrapEndTime(Manager* manager);

void manager_incrementPluginError(Manager* manager);
const gchar* manager_getHostsRootPath(Manager* manager);

void manager_updateMinTimeJump(Manager* manager, gdouble minPathLatency);

void manager_run(Manager*);
gboolean manager_schedulerIsRunning(Manager* manager);

/* info received from master to set up the simulation */
void manager_addNewProgram(Manager* manager, const gchar* name, const gchar* path, const gchar* startSymbol);
void manager_addNewVirtualHost(Manager* manager, HostParameters* params);
void manager_addNewVirtualProcess(Manager* manager, gchar* hostName, gchar* pluginName, gchar* preloadName,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments);

void manager_storeCounts(Manager* manager, ObjectCounter* objectCounter);
void manager_countObject(ObjectType otype, CounterType ctype);

#endif /* SHD_MANAGER_H_ */
