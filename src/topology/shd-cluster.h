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
void cluster_ref(Cluster* cluster);
void cluster_unref(Cluster* cluster);

void cluster_addPoI(Cluster* cluster, in_addr_t networkIP);
guint cluster_getPoICount(Cluster* cluster);
in_addr_t cluster_getRandomPoI(Cluster* cluster, Random* randomSourcePool);

const gchar* cluster_getGeoCode(Cluster* cluster);

void cluster_setPacketLoss(Cluster* cluster, gdouble packetLoss);
gdouble cluster_getPacketLoss(Cluster* cluster);
void cluster_setBandwidthDown(Cluster* cluster, guint bandwidthDown);
guint cluster_getBandwidthDown(Cluster* cluster);
void cluster_setBandwidthUp(Cluster* cluster, guint bandwidthUp);
guint cluster_getBandwidthUp(Cluster* cluster);

#endif /* SHD_CLUSTER_H_ */
