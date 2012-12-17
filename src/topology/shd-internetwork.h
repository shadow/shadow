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

/**
 * An opaque data structure representing a collection of networks and nodes
 * belonging to those networks. This is built up from the simulation input XML
 * file before the simulation starts.
 *
 * @warning Once built, an Internetwork structure should not change until the
 * simulation is complete since multiple threads might be concurrently reading
 * the information stored within using its accessors. internetwork_setReadOnly()
 * should be used to prevent further writes after all components are added.
 *
 * @see internetwork_setReadOnly()
 */
typedef struct _Internetwork Internetwork;

/**
 * Create an Internetwork and its associated inner structures. This structure
 * should be freed with internetwork_free()
 *
 * @return the new Internetwork, guaranteed not to be NULL
 * @see internetwork_free()
 */
Internetwork* internetwork_new();

/**
 * Free the structures associated with the given internet
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 *
 * @see internetwork_new()
 */
void internetwork_free(Internetwork* internet);

/** @note the following are used to build the Internetwork, and can not be used after
 * it has been set read-only */

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param networkID
 * @param bandwidthdown
 * @param bandwidthup
 */
void internetwork_createNetwork(Internetwork* internet, GQuark networkID,
		guint64 bandwidthdown, guint64 bandwidthup, gdouble packetloss);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param sourceClusterID
 * @param destinationClusterID
 * @param latency
 * @param jitter
 * @param packetloss
 */
void internetwork_connectNetworks(Internetwork* internet, GQuark sourceClusterID, GQuark destinationClusterID,
		guint64 latency, guint64 jitter, gdouble packetloss, guint64 latencymin, guint64 latencyQ1,
		guint64 latencymean, guint64 latencyQ3, guint64 latencymax);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param nodeID
 * @param network
 * @param software
 * @param hostname
 * @param bwDownKiBps
 * @param bwUpKiBps
 * @param cpuFrequency
 * @param cpuThreshold
 * @param nodeSeed
 *
 * return the created node
 */
gpointer internetwork_createNode(Internetwork* internet, GQuark nodeID,
		Network* network, GString* hostname,
		guint64 bwDownKiBps, guint64 bwUpKiBps, guint cpuFrequency, gint cpuThreshold, gint cpuPrecision,
		guint nodeSeed, SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel,
		GLogLevelFlags logLevel, gchar logPcap, gchar *pcapDir); /* XXX: return type is "Node*" */

/**
 * Marks the given internet as read-only, so no additional nodes or networks may
 * be created or connected
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 */
void internetwork_setReadOnly(Internetwork* internet);

/** @note the following may be used after the Internetwork is built and set read-only */

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param networkID
 * @return
 */
Network* internetwork_getNetwork(Internetwork* internet, GQuark networkID);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param randomDouble
 * @return
 */
Network* internetwork_getRandomNetwork(Internetwork* internet,
		gdouble randomDouble);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param ip
 * @return
 */
Network* internetwork_lookupNetwork(Internetwork* internet, in_addr_t ip);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param nodeID
 * @return
 */
gpointer internetwork_getNode(Internetwork* internet, GQuark nodeID);/* XXX: return type is "Node*" */

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @return
 */
GList* internetwork_getAllNodes(Internetwork* internet);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param name
 * @return
 */
GQuark internetwork_resolveName(Internetwork* internet, gchar* name);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param ip
 * @return
 */
const gchar* internetwork_resolveIP(Internetwork* internet, guint32 ip);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param id
 * @return
 */
const gchar* internetwork_resolveID(Internetwork* internet, GQuark id);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param sourceNodeID
 * @param destinationNodeID
 * @return
 */
gdouble internetwork_getReliability(Internetwork* internet, GQuark sourceNodeID,
		GQuark destinationNodeID);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param sourceNodeID
 * @param destinationNodeID
 * @param percentile
 * @return
 */
gdouble internetwork_getLatency(Internetwork* internet, GQuark sourceNodeID,
		GQuark destinationNodeID, gdouble percentile);

/**
 *
 * @param internet a valid, non-NULL Internetwork structure previously created
 * with internetwork_new()
 * @param sourceNodeID
 * @param destinationNodeID
 * @return
 */
gdouble internetwork_sampleLatency(Internetwork* internet, GQuark sourceNodeID,
		GQuark destinationNodeID);

/* TODO refactor these out */
guint32 internetwork_getNodeBandwidthUp(Internetwork* internet, GQuark nodeID);
guint32 internetwork_getNodeBandwidthDown(Internetwork* internet, GQuark nodeID);

#endif /* SHD_INTERNETWORK_H_ */
