/**
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

	return internet;
}

void internetwork_free(Internetwork* internet) {
	MAGIC_ASSERT(internet);

	g_hash_table_destroy(internet->nodes);
	g_hash_table_destroy(internet->networks);

	MAGIC_CLEAR(internet);
	g_free(internet);
}

void internetwork_createNetwork(Internetwork* internet, GQuark networkID, CumulativeDistribution* intranetLatency) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	Network* network = network_new(networkID, intranetLatency);
	g_hash_table_replace(internet->networks, &(network->id), network);
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
}

Network* internetwork_getNetwork(Internetwork* internet, GQuark networkID) {
	MAGIC_ASSERT(internet);
	return (Network*) g_hash_table_lookup(internet->networks, &networkID);
}

void internetwork_createNode(Internetwork* internet, GQuark nodeID,
		Network* network, Software* software, GString* hostname,
		guint32 bwDownKiBps, guint32 bwUpKiBps, guint64 cpuBps) {
	MAGIC_ASSERT(internet);
	g_assert(!internet->isReadOnly);

	Node* node = node_new(nodeID, network, software, hostname, bwDownKiBps, bwUpKiBps, cpuBps);
	g_hash_table_replace(internet->nodes, &(node->id), node);
}

Node* internetwork_getNode(Internetwork* internet, GQuark nodeID) {
	MAGIC_ASSERT(internet);
	return (Node*) g_hash_table_lookup(internet->nodes, &nodeID);
}

GList* internetwork_getAllNodes(Internetwork* internet) {
	MAGIC_ASSERT(internet);
	return g_hash_table_get_values(internet->nodes);
}

GQuark internet_resolveName(Internetwork* internet, gchar* name) {
	return g_quark_try_string((const gchar*) name);
}

const gchar* internet_resolveID(Internetwork* internet, GQuark id) {
	return g_quark_to_string(id);
}
