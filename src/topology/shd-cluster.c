/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* a poi cluster that represents a country */
struct _Cluster {
	gchar* geocode;
	GQueue* poiIPs;
	guint bandwidthUp;
	guint bandwidthDown;
	gdouble packetLoss;
	MAGIC_DECLARE;
};

Cluster* cluster_new(const gchar* geocode) {
	Cluster* cluster = g_new0(Cluster, 1);
	MAGIC_INIT(cluster);
	cluster->geocode = g_strdup(geocode);
	cluster->poiIPs = g_queue_new();
	return cluster;
}

void cluster_free(Cluster* cluster) {
	MAGIC_ASSERT(cluster);

	g_queue_free(cluster->poiIPs);

	MAGIC_CLEAR(cluster);
	g_free(cluster);
}

void cluster_addPoI(Cluster* cluster, in_addr_t networkIP) {
	MAGIC_ASSERT(cluster);
	g_queue_push_tail(cluster->poiIPs, GUINT_TO_POINTER(networkIP));
}

void cluster_setPacketLoss(Cluster* cluster, gdouble packetLoss) {
	MAGIC_ASSERT(cluster);
	cluster->packetLoss = packetLoss;
}

void cluster_setBandwidthDown(Cluster* cluster, guint bandwidthDown) {
	MAGIC_ASSERT(cluster);
	cluster->bandwidthDown = bandwidthDown;
}

void cluster_setBandwidthUp(Cluster* cluster, guint bandwidthUp) {
	MAGIC_ASSERT(cluster);
	cluster->bandwidthUp = bandwidthUp;
}
