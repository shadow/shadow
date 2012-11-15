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

struct _ConnectNetworkAction {
	Action super;
	GQuark sourceClusterID;
	GQuark destinationClusterID;
	guint64 latency;
	guint64 jitter;
	gdouble packetloss;
	guint64 latencymin;
	guint64 latencyQ1;
	guint64 latencymean;
	guint64 latencyQ3;
	guint64 latencymax;
	MAGIC_DECLARE;
};

RunnableFunctionTable connectnetwork_functions = {
		(RunnableRunFunc) connectnetwork_run,
		(RunnableFreeFunc) connectnetwork_free,
		MAGIC_VALUE
};

ConnectNetworkAction* connectnetwork_new(GString* clusters, guint64 latency, guint64 jitter, gdouble packetloss,
		guint64 latencymin, guint64 latencyQ1, guint64 latencymean, guint64 latencyQ3, guint64 latencymax) {
	g_assert(clusters);
	ConnectNetworkAction* action = g_new0(ConnectNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &connectnetwork_functions);

	/* parse clusters string like "ABCD DCBA" into separate IDs */
	gchar** tokens = g_strsplit(clusters->str, " ", 2);
	g_assert(g_strrstr(tokens[1], " ") == NULL);

	action->sourceClusterID = g_quark_from_string(tokens[0]);
	action->destinationClusterID = g_quark_from_string(tokens[1]);

	g_strfreev(tokens);

	/* copy the other network properties */
	action->latency = latency;
	action->jitter = jitter;
	action->packetloss = packetloss;
	action->latencymin = latencymin;
	action->latencyQ1 = latencyQ1;
	action->latencymean = latencymean;
	action->latencyQ3 = latencyQ3;
	action->latencymax = latencymax;

	return action;
}

void connectnetwork_run(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

	internetwork_connectNetworks(worker_getInternet(),
			action->sourceClusterID, action->destinationClusterID,
			action->latency, action->jitter, action->packetloss,
			action->latencymin, action->latencyQ1, action->latencymean,
			action->latencyQ3, action->latencymax);

}

void connectnetwork_free(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
