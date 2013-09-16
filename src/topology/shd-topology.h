/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TOPOLOGY_H_
#define SHD_TOPOLOGY_H_

#include "shadow.h"

typedef struct _Topology Topology;

Topology* topology_new(gchar* graphPath);
void topology_free(Topology* top);

gboolean topology_connect(Topology* top, Address* address, gchar* requestedCluster);
void topology_disconnect(Topology* top, Address* address);
gboolean topology_isRoutable(Topology* top, Address* srcAddress, Address* dstAddress);
gdouble topology_getLatency(Topology* top, Address* srcAddress, Address* dstAddress);
gdouble topology_getReliability(Topology* top, Address* srcAddress, Address* dstAddress);

#endif /* SHD_TOPOLOGY_H_ */
