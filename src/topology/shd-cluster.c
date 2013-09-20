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
	gint refCount;
	MAGIC_DECLARE;
};

Cluster* cluster_new(const gchar* geocode) {
	Cluster* cluster = g_new0(Cluster, 1);
	MAGIC_INIT(cluster);
	cluster->geocode = g_strdup(geocode);
	cluster->poiIPs = g_queue_new();
	cluster->refCount = 1;
	return cluster;
}

static void _cluster_free(Cluster* cluster) {
	MAGIC_ASSERT(cluster);

	g_queue_free(cluster->poiIPs);

	MAGIC_CLEAR(cluster);
	g_free(cluster);
}

void cluster_ref(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	cluster->refCount++;
}

void cluster_unref(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	cluster->refCount--;
	if(cluster->refCount < 0) {
		_cluster_free(cluster);
	}
}

void cluster_addPoI(Cluster* cluster, in_addr_t networkIP) {
	MAGIC_ASSERT(cluster);
	g_queue_push_tail(cluster->poiIPs, GUINT_TO_POINTER(networkIP));
}

guint cluster_getPoICount(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	return g_queue_get_length(cluster->poiIPs);
}

in_addr_t cluster_getRandomPoI(Cluster* cluster, Random* randomSourcePool) {
	MAGIC_ASSERT(cluster);
	g_assert(randomSourcePool);

	gdouble randomDouble = random_nextDouble(randomSourcePool);
	guint length = g_queue_get_length(cluster->poiIPs);
	if(length == 0) {
		return 0;
	}

	guint index = (guint) round((length - 1) * randomDouble);
	gpointer poiIPPtr = g_queue_peek_nth(cluster->poiIPs, index);

	return (in_addr_t) GPOINTER_TO_UINT(poiIPPtr);
}

const gchar* cluster_getGeoCode(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	return cluster->geocode;
}

void cluster_setPacketLoss(Cluster* cluster, gdouble packetLoss) {
	MAGIC_ASSERT(cluster);
	g_assert(packetLoss >= 0.0 && packetLoss <= 1.0);
	cluster->packetLoss = packetLoss;
}

gdouble cluster_getPacketLoss(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	return cluster->packetLoss;
}

void cluster_setBandwidthDown(Cluster* cluster, guint bandwidthDown) {
	MAGIC_ASSERT(cluster);
	cluster->bandwidthDown = bandwidthDown;
}

guint cluster_getBandwidthDown(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	return cluster->bandwidthDown;
}

void cluster_setBandwidthUp(Cluster* cluster, guint bandwidthUp) {
	MAGIC_ASSERT(cluster);
	cluster->bandwidthUp = bandwidthUp;
}

guint cluster_getBandwidthUp(Cluster* cluster) {
	MAGIC_ASSERT(cluster);
	return cluster->bandwidthUp;
}
