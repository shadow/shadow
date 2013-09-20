/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CLUSTER_H_
#define SHD_CLUSTER_H_

#include "shadow.h"

typedef struct _Cluster Cluster;

Cluster* cluster_new();
void cluster_free(Cluster* cluster);
void cluster_addPoI(Cluster* cluster, in_addr_t networkIP);
void cluster_setPacketLoss(Cluster* cluster, gdouble packetLoss);
void cluster_setBandwidthDown(Cluster* cluster, guint bandwidthDown);
void cluster_setBandwidthUp(Cluster* cluster, guint bandwidthUp);

#endif /* SHD_CLUSTER_H_ */
