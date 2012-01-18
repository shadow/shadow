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

#ifndef SHD_INTERNETWORK_H_
#define SHD_INTERNETWORK_H_

#include "shadow.h"

typedef struct _Internetwork Internetwork;

struct _Internetwork {
	gboolean isReadOnly;

	GHashTable* nodes;
	GHashTable* networks;
	GHashTable* networksByIP;
	GHashTable* nameByIp;
	GHashTable* ipByName;

	gdouble maximumGlobalLatency;
	gdouble minimumGlobalLatency;

	guint32 ipCounter;

	MAGIC_DECLARE;
};

Internetwork* internetwork_new();
void internetwork_free(Internetwork* internet);

void internetwork_setReadOnly(Internetwork* internet);

void internetwork_createNetwork(Internetwork* internet, GQuark networkID, guint64 bandwidthdown, guint64 bandwidthup);
void internetwork_connectNetworks(Internetwork* internet,
		GQuark sourceClusterID, GQuark destinationClusterID,
		guint64 latency, guint64 jitter, gdouble packetloss);
Network* internetwork_getNetwork(Internetwork* internet, GQuark networkID);
Network* internetwork_getRandomNetwork(Internetwork* internet);
Network* internetwork_lookupNetwork(Internetwork* internet, in_addr_t ip);

void internetwork_createNode(Internetwork* internet, GQuark nodeID,
		Network* network, Software* software, GString* hostname,
		guint64 bwDownKiBps, guint64 bwUpKiBps, guint64 cpuBps);
gpointer internetwork_getNode(Internetwork* internet, GQuark nodeID);/* XXX: return type is "Node*" */
GList* internetwork_getAllNodes(Internetwork* internet);

GQuark internetwork_resolveName(Internetwork* internet, gchar* name);
const gchar* internetwork_resolveIP(Internetwork* internet, guint32 ip);
const gchar* internetwork_resolveID(Internetwork* internet, GQuark id);

gdouble internetwork_getReliability(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID);
gdouble internetwork_getLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID, gdouble percentile);
gdouble internetwork_sampleLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID);
gdouble internetwork_getMinimumGlobalLatency(Internetwork* internet);
gdouble internetwork_getMaximumGlobalLatency(Internetwork* internet);
guint32 internetwork_getNodeBandwidthUp(Internetwork* internet, GQuark nodeID);
guint32 internetwork_getNodeBandwidthDown(Internetwork* internet, GQuark nodeID);

#endif /* SHD_INTERNETWORK_H_ */
