/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-action-internal.h"

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
