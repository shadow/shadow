/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CONNECT_NETWORK_H_
#define SHD_CONNECT_NETWORK_H_

#include "shadow.h"

typedef struct _ConnectNetworkAction ConnectNetworkAction;

ConnectNetworkAction* connectnetwork_new(GString* startCluster, GString* endCluster, guint64 latency, guint64 jitter, gdouble packetloss,
		guint64 latencymin, guint64 latencyQ1, guint64 latencymean, guint64 latencyQ3, guint64 latencymax);
void connectnetwork_run(ConnectNetworkAction* action);
void connectnetwork_free(ConnectNetworkAction* action);

#endif /* SHD_CONNECT_NETWORK_H_ */
