/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"

Internetwork* internetwork_new() {
	Internetwork* internet = g_new0(Internetwork, 1);
	MAGIC_INIT(internet);

	internet->nodes = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, node_free);
	internet->networks = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, network_free);
	internet->ipByName = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
	internet->nameByIp = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

	return internet;
}

void internetwork_free(Internetwork* internet) {
	MAGIC_ASSERT(internet);

	/* free all applications before freeing any of the nodes since freeing
	 * applications may cause close() to get called on sockets which needs
	 * other node information.
	 */
	g_hash_table_foreach(internet->nodes, node_stopApplication, NULL);

	/* now cleanup the rest */
	g_hash_table_destroy(internet->nodes);
	g_hash_table_destroy(internet->networks);
	g_hash_table_destroy(internet->ipByName);
	g_hash_table_destroy(internet->nameByIp);

	MAGIC_CLEAR(internet);
	g_free(internet);
}

static void _internetwork_trackLatency(Internetwork* internet, CumulativeDistribution* latency) {
	MAGIC_ASSERT(internet);
	gdouble maxLocal = cdf_getValue(latency, 1.0);
	if(maxLocal > internet->maximumGlobalLatency) {
		internet->maximumGlobalLatency = maxLocal;
	}
	gdouble minLocal = cdf_getValue(latency, 0.0);
	if(minLocal < internet->minimumGlobalLatency) {
		internet->minimumGlobalLatency = minLocal;
	}
}

void internetwork_createNetwork(Internetwork* internet, GQuark networkID, CumulativeDistribution* intranetLatency, gdouble intranetReliability) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	Network* network = network_new(networkID);
	g_hash_table_replace(internet->networks, &(network->id), network);

	Link* selfLink = link_new(network, network, intranetLatency, intranetReliability);
	network_addOutgoingLink(network, selfLink);

	_internetwork_trackLatency(internet, intranetLatency);
}

void internetwork_connectNetworks(Internetwork* internet, GQuark networkAID, GQuark networkBID,
		CumulativeDistribution* latencyA2B, CumulativeDistribution* latencyB2A,
		gdouble reliabilityA2B, gdouble reliabilityB2A) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	/* lookup our networks */
	Network* networkA = internetwork_getNetwork(internet, networkAID);
	Network* networkB = internetwork_getNetwork(internet, networkBID);
	g_assert(networkA && networkB);

	/* create the links */
	Link* linkA2B = link_new(networkA, networkB, latencyA2B, reliabilityA2B);
	Link* linkB2A = link_new(networkB, networkA, latencyB2A, reliabilityB2A);

	/* build links into topology */
	network_addOutgoingLink(networkA, linkA2B);
	network_addOutgoingLink(networkB, linkB2A);

	network_addIncomingLink(networkA, linkB2A);
	network_addIncomingLink(networkB, linkA2B);

	/* track latency */
	_internetwork_trackLatency(internet, latencyA2B);
	_internetwork_trackLatency(internet, latencyB2A);
}

Network* internetwork_getNetwork(Internetwork* internet, GQuark networkID) {
	MAGIC_ASSERT(internet);
	return (Network*) g_hash_table_lookup(internet->networks, &networkID);
}

guint32 _internetwork_generateIP(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	internet->ipCounter++;
	while(internet->ipCounter == htonl(INADDR_NONE) ||
			internet->ipCounter == htonl(INADDR_ANY) ||
			internet->ipCounter == htonl(INADDR_LOOPBACK) ||
			internet->ipCounter == htonl(INADDR_BROADCAST))
	{
		internet->ipCounter++;
	}
	return internet->ipCounter;
}

void internetwork_createNode(Internetwork* internet, GQuark nodeID,
		Network* network, Software* software, GString* hostname,
		guint32 bwDownKiBps, guint32 bwUpKiBps, guint64 cpuBps) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	guint32 ip = _internetwork_generateIP(internet);
	Node* node = node_new(nodeID, network, software, ip, hostname, bwDownKiBps, bwUpKiBps, cpuBps);
	g_hash_table_replace(internet->nodes, &(node->id), node);

	gchar* mapName = g_strdup((const gchar*) hostname->str);
	guint32* mapIP = g_new0(guint32, 1);
	*mapIP = ip;
	g_hash_table_replace(internet->ipByName, mapName, mapIP);
	g_hash_table_replace(internet->nameByIp, mapIP, mapName);
}

Node* internetwork_getNode(Internetwork* internet, GQuark nodeID) {
	MAGIC_ASSERT(internet);
	return (Node*) g_hash_table_lookup(internet->nodes, &nodeID);
}

GList* internetwork_getAllNodes(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	return g_hash_table_get_values(internet->nodes);
}

guint32 internetwork_resolveName(Internetwork* internet, gchar* name) {
	MAGIC_ASSERT(internet);
	return g_quark_try_string((const gchar*) name);
//	guint32* ip = g_hash_table_lookup(internet->ipByName, name);
//	if(ip) {
//		return *ip;
//	} else {
//		return (guint32)INADDR_NONE;
//	}
}

const gchar* internetwork_resolveIP(Internetwork* internet, guint32 ip) {
	MAGIC_ASSERT(internet);
	return g_hash_table_lookup(internet->nameByIp, &ip);
}

const gchar* internetwork_resolveID(Internetwork* internet, GQuark id) {
	MAGIC_ASSERT(internet);
	return g_quark_to_string(id);
}

gdouble internetwork_getMaximumGlobalLatency(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	return internet->maximumGlobalLatency;
}

gdouble internetwork_getMinimumGlobalLatency(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	return internet->minimumGlobalLatency;
}

//guint32 internetwork_getNodeBandwidthUp(Internetwork* internet, GQuark nodeID) {
//	MAGIC_ASSERT(internet);
//	Node* node = internetwork_getNode(internet, nodeID);
//	return node_getBandwidthUp(node);
//}
//
//guint32 internetwork_getNodeBandwidthDown(Internetwork* internet, GQuark nodeID) {
//	MAGIC_ASSERT(internet);
//	Node* node = internetwork_getNode(internet, nodeID);
//	return node_getBandwidthDown(node);
//}

gdouble internetwork_getReliability(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	MAGIC_ASSERT(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	MAGIC_ASSERT(destinationNode);
	return network_getLinkReliability(sourceNode->network, destinationNode->network);
}

gdouble internetwork_getLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID, gdouble percentile) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	MAGIC_ASSERT(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	MAGIC_ASSERT(destinationNode);
	return network_getLinkLatency(sourceNode->network, destinationNode->network, percentile);
}

gdouble internetwork_sampleLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	MAGIC_ASSERT(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	MAGIC_ASSERT(destinationNode);
	return network_sampleLinkLatency(sourceNode->network, destinationNode->network);
}
