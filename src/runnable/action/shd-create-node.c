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

RunnableFunctionTable createnodes_functions = {
		(RunnableRunFunc) createnodes_run,
		(RunnableFreeFunc) createnodes_free,
		MAGIC_VALUE
};

CreateNodesAction* createnodes_new(guint64 quantity, GString* name,
		GString* applicationName, GString* cpudelayCDFName,
		GString* networkName, GString* bandwidthupCDFName,
		GString* bandwidthdownCDFName)
{
	g_assert(name && applicationName && cpudelayCDFName && networkName && bandwidthupCDFName && bandwidthdownCDFName);
	CreateNodesAction* action = g_new0(CreateNodesAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnodes_functions);

	action->quantity = quantity;
	action->id = g_quark_from_string((const gchar*) name->str);
	action->softwareID = g_quark_from_string((const gchar*) applicationName->str);
	action->cpudelayCDFID = g_quark_from_string((const gchar*) cpudelayCDFName->str);
	action->networkID = g_quark_from_string((const gchar*) networkName->str);
	action->bandwidthupID = g_quark_from_string((const gchar*) bandwidthupCDFName->str);
	action->bandwidthdownID = g_quark_from_string((const gchar*) bandwidthdownCDFName->str);

	return action;
}

void createnodes_run(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	CumulativeDistribution* bwUpCDF = engine_get(worker->cached_engine, CDFS, action->bandwidthupID);
	CumulativeDistribution* bwDownCDF = engine_get(worker->cached_engine, CDFS, action->bandwidthdownID);
	CumulativeDistribution* cpuCDF = engine_get(worker->cached_engine, CDFS, action->cpudelayCDFID);
	Software* software = engine_get(worker->cached_engine, SOFTWARE, action->softwareID);

	Network* network = internetwork_getNetwork(worker->cached_engine->internet, action->networkID);

	/* must have one cdf, but the other one can be anything if not a cdf, it will be ignored */
	if(!bwUpCDF && !bwDownCDF) {
		critical("Invalid XML file submitted. Please use at least one bandwidth cdf for node creation.");
		return;
	}

	if(!cpuCDF || !network || !software) {
		critical("Can not create %lu Node(s) '%s' with NULL components. Check XML file for errors.",
				action->quantity, g_quark_to_string(action->id));
		return;
	}

	guint hostnameCounter = 0;

	for(gint i = 0; i < action->quantity; i++) {
		/* get bandwidth */
		guint32 bwUpKiBps = 0;
		guint32 bwDownKiBps = 0;

		/* if we only have 1 id, we have symmetric bandwidth, otherwise asym as specified by cdfs */
		if(!bwUpCDF || !bwDownCDF) {
			CumulativeDistribution* symCDF = bwUpCDF != NULL ? bwUpCDF : bwDownCDF;
			bwUpKiBps = bwDownKiBps = (guint32) cdf_getRandomValue(symCDF);
		} else {
			bwUpKiBps = (guint32) cdf_getRandomValue(bwUpCDF);
			bwDownKiBps = (guint32) cdf_getRandomValue(bwDownCDF);
		}

		guint64 cpuBps = (guint64) cdf_getRandomValue(cpuCDF);

		/* hostname */
		GString* hostname = g_string_new(g_quark_to_string(action->id));
		if(action->quantity > 1) {
			gchar prefix[20];
			g_snprintf(prefix, 20, "%u.", ++hostnameCounter);
			hostname = g_string_prepend(hostname, (const char*) prefix);
		}
		GQuark id = g_quark_from_string((const gchar*) hostname->str);

		/* the node is part of the internet */
		internetwork_createNode(worker->cached_engine->internet, id, network, software, hostname, bwDownKiBps, bwUpKiBps, cpuBps);

		g_string_free(hostname, TRUE);

		StartApplicationEvent* event = startapplication_new();

		/* make sure our bootstrap events are set properly */
		worker->clock_now = 0;
		worker_scheduleEvent((Event*)event, software->startTime, id);
		worker->clock_now = 0;
	}
}

void createnodes_free(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
