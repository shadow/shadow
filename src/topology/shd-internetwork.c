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

#include "shadow.h"

struct _Internetwork {
	/** if set, dont do anything that changes our data */
	gboolean isReadOnly;

	/** all the nodes in our simulation, by ID */
	GHashTable* nodes;

	/** all the networks in our simulation, by ID */
	GHashTable* networks;
	/** contains the same networks as above, but keyed by IP */
	GHashTable* networksByIP;

	/** hostnames and IPs */
	GHashTable* nameByIp;
	GHashTable* ipByName;

	/** the maximum latency of all links between all networks we are tracking */
	gdouble maximumGlobalLatency;

	/** the minimum latency of all links between all networks we are tracking */
	gdouble minimumGlobalLatency;

	/** used for IP generation */
	guint32 ipCounter;

	MAGIC_DECLARE;
};

Internetwork* internetwork_new() {
	Internetwork* internet = g_new0(Internetwork, 1);
	MAGIC_INIT(internet);

	/* create our data structures, with the correct destructors */

	internet->nodes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
	internet->networks = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, network_free);
	internet->networksByIP = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);
	internet->ipByName = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free);
	internet->nameByIp = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

	return internet;
}

void internetwork_free(Internetwork* internet) {
	MAGIC_ASSERT(internet);

	/* now cleanup the rest */
	g_hash_table_destroy(internet->nodes);
	g_hash_table_destroy(internet->networks);
	g_hash_table_destroy(internet->networksByIP);
	g_hash_table_destroy(internet->ipByName);
	g_hash_table_destroy(internet->nameByIp);

	MAGIC_CLEAR(internet);
	g_free(internet);
}

void internetwork_setReadOnly(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	internet->isReadOnly = TRUE;
}

static void _internetwork_trackLatency(Internetwork* internet, Link* link) {
	MAGIC_ASSERT(internet);

	guint64 latency = link_getLatency(link);
	guint64 jitter = link_getJitter(link);

	internet->maximumGlobalLatency = MAX(internet->maximumGlobalLatency, (latency+jitter));
	internet->minimumGlobalLatency = MIN(internet->minimumGlobalLatency, (latency-jitter));
}

void internetwork_createNetwork(Internetwork* internet, GQuark networkID,
		guint64 bandwidthdown, guint64 bandwidthup, gdouble packetloss) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	Network* network = network_new(networkID, bandwidthdown, bandwidthup, packetloss);
	g_hash_table_replace(internet->networks, network_getIDReference(network), network);
}

void internetwork_connectNetworks(Internetwork* internet, GQuark sourceClusterID, GQuark destinationClusterID,
		guint64 latency, guint64 jitter, gdouble packetloss, guint64 latencymin, guint64 latencyQ1,
		guint64 latencymean, guint64 latencyQ3, guint64 latencymax) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	/* lookup our networks */
	Network* sourceNetwork = internetwork_getNetwork(internet, sourceClusterID);
	Network* destinationNetwork = internetwork_getNetwork(internet, destinationClusterID);
	g_assert(sourceNetwork && destinationNetwork);

	/* create the links */
	Link* link = link_new(sourceNetwork, destinationNetwork, latency, jitter, packetloss,
			latencymin, latencyQ1, latencymean, latencyQ3, latencymax);
	network_addLink(sourceNetwork, link);
	_internetwork_trackLatency(internet, link);

	/* if not the same clusters, create the reverse link */
	if(sourceClusterID != destinationClusterID) {
	    link = link_new(destinationNetwork, sourceNetwork, latency, jitter, packetloss,
				latencymin, latencyQ1, latencymean, latencyQ3, latencymax);
	    network_addLink(destinationNetwork, link);
        _internetwork_trackLatency(internet, link);
	}
}

Network* internetwork_getNetwork(Internetwork* internet, GQuark networkID) {
	MAGIC_ASSERT(internet);
	return (Network*) g_hash_table_lookup(internet->networks, &networkID);
}

Network* internetwork_getRandomNetwork(Internetwork* internet, gdouble randomDouble) {
	MAGIC_ASSERT(internet);

	/* TODO this is ugly.
	 * I cant believe the g_list iterates the list to count the length...
	 */

	GList* networkList = g_hash_table_get_values(internet->networks);
	guint length = g_list_length(networkList);

	guint n = (guint)(((gdouble)length) * randomDouble);
	g_assert((n >= 0) && (n <= length));

	Network* network = (Network*) g_list_nth_data(networkList, n);
	g_list_free(networkList);

	return network;
}

Network* internetwork_lookupNetwork(Internetwork* internet, in_addr_t ip) {
	MAGIC_ASSERT(internet);
	return (Network*) g_hash_table_lookup(internet->networksByIP, &ip);
}

static guint32 _internetwork_generateIP(Internetwork* internet) {
	MAGIC_ASSERT(internet);

	/* FIXME: there are many more restricted IP ranges
	 * e.g. 192.168..., 10.0.0.0/8, etc.
	 * there is an RFC that defines these.
	 */

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

/* XXX: return type is "Node*" */
gpointer internetwork_createNode(Internetwork* internet, GQuark nodeID,
		Network* network, GString* hostname,
		guint64 bwDownKiBps, guint64 bwUpKiBps, guint cpuFrequency, gint cpuThreshold, gint cpuPrecision,
		guint nodeSeed, SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel,
		GLogLevelFlags logLevel, gchar logPcap, gchar *pcapDir, gchar* qdisc) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	guint32 ip = _internetwork_generateIP(internet);
	ip = (guint32) nodeID;
	Node* node = node_new(nodeID, network, ip, hostname, bwDownKiBps, bwUpKiBps,
			cpuFrequency, cpuThreshold, cpuPrecision, nodeSeed, heartbeatInterval, heartbeatLogLevel,
			logLevel, logPcap, pcapDir, qdisc);
	g_hash_table_replace(internet->nodes, GUINT_TO_POINTER((guint)nodeID), node);

	gchar* mapName = g_strdup((const gchar*) hostname->str);
	guint32* mapIP = g_new0(guint32, 1);
	*mapIP = ip;
	g_hash_table_replace(internet->networksByIP, mapIP, network);
	g_hash_table_replace(internet->ipByName, mapName, mapIP);
	g_hash_table_replace(internet->nameByIp, mapIP, mapName);

	return node;
}

/* XXX: return type is "Node*" */
gpointer internetwork_getNode(Internetwork* internet, GQuark nodeID) {
	MAGIC_ASSERT(internet);
	return (Node*) g_hash_table_lookup(internet->nodes, GUINT_TO_POINTER((guint)nodeID));
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

guint32 internetwork_getNodeBandwidthUp(Internetwork* internet, GQuark nodeID) {
	MAGIC_ASSERT(internet);
	Node* node = internetwork_getNode(internet, nodeID);
	NetworkInterface* interface = node_lookupInterface(node, nodeID);
	return networkinterface_getSpeedUpKiBps(interface);
}

guint32 internetwork_getNodeBandwidthDown(Internetwork* internet, GQuark nodeID) {
	MAGIC_ASSERT(internet);
	Node* node = internetwork_getNode(internet, nodeID);
	NetworkInterface* interface = node_lookupInterface(node, nodeID);
	return networkinterface_getSpeedDownKiBps(interface);
}

gdouble internetwork_getReliability(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	in_addr_t sourceIP = node_getDefaultIP(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	in_addr_t destinationIP = node_getDefaultIP(destinationNode);
	return network_getLinkReliability(sourceIP, destinationIP);
}

gdouble internetwork_getLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID, gdouble percentile) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	in_addr_t sourceIP = node_getDefaultIP(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	in_addr_t destinationIP = node_getDefaultIP(destinationNode);
	return network_getLinkLatency(sourceIP, destinationIP, percentile);
}

gdouble internetwork_sampleLatency(Internetwork* internet, GQuark sourceNodeID, GQuark destinationNodeID) {
	MAGIC_ASSERT(internet);
	Node* sourceNode = internetwork_getNode(internet, sourceNodeID);
	in_addr_t sourceIP = node_getDefaultIP(sourceNode);
	Node* destinationNode = internetwork_getNode(internet, destinationNodeID);
	in_addr_t destinationIP = node_getDefaultIP(destinationNode);
	return network_sampleLinkLatency(sourceIP, destinationIP);
}
