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

ConnectNetworkAction* connectnetwork_new(GString* startCluster, GString* endCluster,
		guint64 latency, guint64 jitter, gdouble packetloss,
		guint64 latencymin, guint64 latencyQ1, guint64 latencymean, guint64 latencyQ3, guint64 latencymax) {
	g_assert(startCluster && endCluster);
	ConnectNetworkAction* action = g_new0(ConnectNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &connectnetwork_functions);

	action->sourceClusterID = g_quark_from_string(startCluster->str);
	action->destinationClusterID = g_quark_from_string(endCluster->str);

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
