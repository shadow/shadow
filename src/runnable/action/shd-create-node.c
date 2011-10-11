/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "shadow.h"

#include <netinet/in.h>

RunnableVTable createnodes_vtable = { (RunnableRunFunc) createnodes_run,
		(RunnableFreeFunc) createnodes_free, MAGIC_VALUE };

CreateNodesAction* createnodes_new(guint64 quantity, GString* name,
		GString* applicationName, GString* cpudelayCDFName,
		GString* networkName, GString* bandwidthupCDFName,
		GString* bandwidthdownCDFName)
{
	g_assert(name && applicationName && cpudelayCDFName && networkName && bandwidthupCDFName && bandwidthdownCDFName);
	CreateNodesAction* action = g_new0(CreateNodesAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnodes_vtable);

	action->quantity = quantity;
	action->id = g_quark_from_string((const gchar*) name->str);
	action->applicationID = g_quark_from_string((const gchar*) applicationName->str);
	action->cpudelayCDFID = g_quark_from_string((const gchar*) cpudelayCDFName->str);
	action->networkID = g_quark_from_string((const gchar*) networkName->str);
	action->bandwidthupID = g_quark_from_string((const gchar*) bandwidthupCDFName->str);
	action->bandwidthdownID = g_quark_from_string((const gchar*) bandwidthdownCDFName->str);

	return action;
}

void createnodes_run(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	CumulativeDistribution* bwUpCDF = registry_get(worker->cached_engine->registry, CDFS, &(action->bandwidthupID));
	CumulativeDistribution* bwDownCDF = registry_get(worker->cached_engine->registry, CDFS, &(action->bandwidthdownID));
	CumulativeDistribution* cpuCDF = registry_get(worker->cached_engine->registry, CDFS, &(action->cpudelayCDFID));
	Network* network = registry_get(worker->cached_engine->registry, NETWORKS, &(action->networkID));
	Application* application = registry_get(worker->cached_engine->registry, APPLICATIONS, &(action->applicationID));

	/* must have one cdf, but the other one can be anything if not a cdf, it will be ignored */
	if(!bwUpCDF && !bwDownCDF) {
		critical("Invalid XML file submitted. Please use at least one bandwidth cdf for node creation.\n");
		return;
	}

	if(!cpuCDF || !network || !application) {
		critical("Node can not be created with NULL components. Check XML file for errors.");
		return;
	}

	guint hostnameCounter = 0;

	for(gint i = 0; i < action->quantity; i++) {
		/* get bandwidth */
		guint32 KBps_up = 0;
		guint32 KBps_down = 0;

		/* if we only have 1 id, we have symmetric bandwidth, otherwise asym as specified by cdfs */
		if(!bwUpCDF || !bwDownCDF) {
			CumulativeDistribution* symCDF = bwUpCDF != NULL ? bwUpCDF : bwDownCDF;
			KBps_up = KBps_down = (guint32) cdf_getRandomValue(symCDF);
		} else {
			KBps_up = (guint32) cdf_getRandomValue(bwUpCDF);
			KBps_down = (guint32) cdf_getRandomValue(bwDownCDF);
		}

		guint64 cpu_speed_Bps = (guint64) cdf_getRandomValue(cpuCDF);

		/* address */
		in_addr_t ipAddress = (in_addr_t) action->id;

		/* hostname */
		GString* hostname = g_string_new(g_quark_to_string(action->id));
		if(action->quantity > 1) {
			gchar prefix[20];
			g_snprintf(prefix, 20, "%u.", ++hostnameCounter);
			hostname = g_string_prepend(hostname, (const char*) prefix);
		}
		GQuark id = g_quark_from_string((const gchar*) hostname->str);

		/* add this nodes hostname, etc,  to resolver map */
		Node* node = node_new(id, network, application, ipAddress, hostname, KBps_down, KBps_up, cpu_speed_Bps);
		registry_put(worker->cached_engine->registry, NODES, &(node->id), node);
		resolver_add(worker->cached_engine->resolver, hostname->str, ipAddress, 0, KBps_down, KBps_up);

		g_string_free(hostname, TRUE);

		StartApplicationEvent* event = startapplication_new();
		worker_scheduleEvent((Event*)event, application->startTime, node->id);
	}
}

void createnodes_free(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
