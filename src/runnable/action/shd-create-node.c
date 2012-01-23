/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

struct _CreateNodesAction {
	Action super;
	GQuark id;
	GQuark softwareID;
	GQuark networkID;
	guint64 bandwidthdown;
	guint64 bandwidthup;
	guint64 quantity;
	MAGIC_DECLARE;
};

RunnableFunctionTable createnodes_functions = {
		(RunnableRunFunc) createnodes_run,
		(RunnableFreeFunc) createnodes_free,
		MAGIC_VALUE
};

CreateNodesAction* createnodes_new(GString* name, GString* software, GString* cluster,
		guint64 bandwidthdown, guint64 bandwidthup, guint64 quantity)
{
	g_assert(name && software);
	CreateNodesAction* action = g_new0(CreateNodesAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnodes_functions);

	action->id = g_quark_from_string((const gchar*) name->str);
	action->softwareID = g_quark_from_string((const gchar*) software->str);

	action->networkID = cluster ? g_quark_from_string((const gchar*) cluster->str) : 0;
	action->bandwidthdown = bandwidthdown;
	action->bandwidthup = bandwidthup;
	action->quantity = quantity ? quantity : 1;

	return action;
}

void createnodes_run(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	const gchar* hostname = g_quark_to_string(action->id);
	guint hostnameCounter = 0;
	Software* software = engine_get(worker->cached_engine, SOFTWARE, action->softwareID);

	if(!hostname || !software) {
		critical("Can not create %lu Node(s) '%s' with NULL components. Check XML file for errors.",
				action->quantity, g_quark_to_string(action->id));
		return;
	}

	/* if they didnt specify a network, assign to a random network */
	Network* assignedNetwork = NULL;
	if(action->networkID) {
		assignedNetwork = internetwork_getNetwork(worker_getInternet(), action->networkID);
		g_assert(assignedNetwork);
	}

	for(gint i = 0; i < action->quantity; i++) {
		gdouble randomDouble = engine_nextRandomDouble(worker->cached_engine);
		Network* network = assignedNetwork ? assignedNetwork :
				internetwork_getRandomNetwork(worker_getInternet(), randomDouble);
		g_assert(network);

		/* use network bandwidth unless an override was given */
		guint64 bwUpKiBps = action->bandwidthup ? action->bandwidthup : network_getBandwidthUp(network);
		guint64 bwDownKiBps = action->bandwidthdown ? action->bandwidthdown : network_getBandwidthDown(network);

		/* FIXME change this when CPU model is added */
		guint64 cpuBps = 0;

		/* hostname */
		GString* hostnameBuffer = g_string_new(hostname);
		if(action->quantity > 1) {
			gchar prefix[20];
			g_snprintf(prefix, 20, "%u.", ++hostnameCounter);
			hostnameBuffer = g_string_prepend(hostnameBuffer, (const char*) prefix);
		}
		GQuark id = g_quark_from_string((const gchar*) hostnameBuffer->str);

		/* the node is part of the internet */
		guint nodeSeed = (guint) engine_nextRandomInt(worker->cached_engine);
		internetwork_createNode(worker_getInternet(), id, network, software, hostnameBuffer, bwDownKiBps, bwUpKiBps, cpuBps, nodeSeed);

		g_string_free(hostnameBuffer, TRUE);

		StartApplicationEvent* event = startapplication_new();

		/* make sure our bootstrap events are set properly */
		worker->clock_now = 0;
		worker_scheduleEvent((Event*)event, software->startTime, id);
		worker->clock_now = SIMTIME_INVALID;
	}
}

void createnodes_free(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
