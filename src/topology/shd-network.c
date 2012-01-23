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

struct _Network {
	GQuark id;
	/* links to other networks this network can access */
	GList* outgoingLinks;
	/* links from other networks that can access this network */
	GList* incomingLinks;
	/* map to outgoing links by network id */
	GHashTable* outgoingLinkMap;
	guint64 bandwidthdown;
	guint64 bandwidthup;
	MAGIC_DECLARE;
};

Network* network_new(GQuark id, guint64 bandwidthdown, guint64 bandwidthup) {
	Network* network = g_new0(Network, 1);
	MAGIC_INIT(network);

	network->id = id;
	network->bandwidthdown = bandwidthdown;
	network->bandwidthup = bandwidthup;

	/* lists are created by setting to NULL */
	network->incomingLinks = NULL;
	network->outgoingLinks = NULL;

	/* the keys will belong to other networks, outgoing links belong to us */
	network->outgoingLinkMap = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, link_free);

	return network;
}

void network_free(gpointer data) {
	Network* network = data;
	MAGIC_ASSERT(network);

	/* outgoing links are destroyed with the linkmap */
	if(network->incomingLinks){
		g_list_free(network->incomingLinks);
	}
	if(network->outgoingLinks) {
		g_list_free(network->outgoingLinks);
	}
	g_hash_table_destroy(network->outgoingLinkMap);

	MAGIC_CLEAR(network);
	g_free(network);
}

GQuark* network_getIDReference(Network* network) {
	MAGIC_ASSERT(network);
	return &(network->id);
}

guint64 network_getBandwidthUp(Network* network) {
	MAGIC_ASSERT(network);
	return network->bandwidthup;
}

guint64 network_getBandwidthDown(Network* network) {
	MAGIC_ASSERT(network);
	return network->bandwidthdown;
}

gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Network* na = a;
	const Network* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->id > nb->id ? +1 : na->id == nb->id ? 0 : -1;
}

gboolean network_isEqual(Network* a, Network* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		return network_compare(a, b, NULL) == 0;
	}
}

void network_addOutgoingLink(Network* network, gpointer outgoingLink) {
	MAGIC_ASSERT(network);

	/* prepending is O(1), but appending is O(n) since it traverses the list */
	network->outgoingLinks = g_list_prepend(network->outgoingLinks, outgoingLink);

	/* FIXME: if this link replaces an existing, the existing will be freed in
	 * the replace call but still exist in the list of outgoing links (and the
	 * list of incoming links at the other network)
	 * this is because we currently only support single links to each network
	 */
	Network* destination = link_getDestinationNetwork((Link*)outgoingLink);
	g_hash_table_replace(network->outgoingLinkMap, &(destination->id), outgoingLink);
}

void network_addIncomingLink(Network* network, gpointer incomingLink) {
	MAGIC_ASSERT(network);

	/* prepending is O(1), but appending is O(n) since it traverses the list */
	network->incomingLinks = g_list_prepend(network->incomingLinks, incomingLink);
}

gdouble network_getLinkReliability(Network* sourceNetwork, Network* destinationNetwork) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);
	Link* link = g_hash_table_lookup(sourceNetwork->outgoingLinkMap, &(destinationNetwork->id));
	if(link) {
		return (1.0 - link_getPacketLoss(link));
	} else {
		critical("unable to find link between networks '%s' and '%s'. Check XML file for errors.",
				g_quark_to_string(sourceNetwork->id), g_quark_to_string(destinationNetwork->id));
		return G_MINDOUBLE;
	}
}

gdouble network_getLinkLatency(Network* sourceNetwork, Network* destinationNetwork, gdouble percentile) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);

	Link* link = g_hash_table_lookup(sourceNetwork->outgoingLinkMap, &(destinationNetwork->id));

	if(link) {
		return link_computeDelay(link, percentile);
	} else {
		critical("unable to find link between networks '%s' and '%s'. Check XML file for errors.",
				g_quark_to_string(sourceNetwork->id), g_quark_to_string(destinationNetwork->id));
		return G_MAXDOUBLE;
	}
}

gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);

	Random* random = node_getRandom(worker_getPrivate()->cached_node);
	gdouble percentile = random_nextDouble(random);
	return network_getLinkLatency(sourceNetwork, destinationNetwork, percentile);
}

void network_scheduleRetransmit(Network* network, Packet* packet) {
	MAGIC_ASSERT(network);

	// FIXME make sure loopback packets dont get here!!
	// FIXME network_isEqual is wrong!

	/* source should retransmit. use latency to approximate RTT for 'retransmit timer' */
	Internetwork* internet = worker_getInternet();

	in_addr_t sourceIP = packet_getSourceIP(packet);
	Network* sourceNetwork = internetwork_lookupNetwork(internet, sourceIP);
	in_addr_t destinationIP = packet_getDestinationIP(packet);
	Network* destinationNetwork = internetwork_lookupNetwork(internet, destinationIP);

	gdouble latency = 0;
	if(network_isEqual(network, sourceNetwork)) {
		/* RTT is two link latencies */
		latency += network_sampleLinkLatency(sourceNetwork, destinationNetwork);
		latency += network_sampleLinkLatency(destinationNetwork, sourceNetwork);
	} else {
		/* latency to destination already incurred, RTT is latency back to source */
		latency += network_sampleLinkLatency(destinationNetwork, sourceNetwork);
	}

	SimulationTime delay = (SimulationTime) floor(latency * SIMTIME_ONE_MILLISECOND);
	PacketDroppedEvent* event = packetdropped_new(packet);
	worker_scheduleEvent((Event*)event, delay, (GQuark)sourceIP);
}

void network_schedulePacket(Network* sourceNetwork, Packet* packet) {
	MAGIC_ASSERT(sourceNetwork);

	Internetwork* internet = worker_getInternet();
	in_addr_t destinationIP = packet_getDestinationIP(packet);
	Network* destinationNetwork = internetwork_lookupNetwork(internet, destinationIP);

	/* first thing to check is if network reliability forces us to 'drop'
	 * the packet. if so, get out of dodge doing as little as possible.
	 */
	gdouble reliability = network_getLinkReliability(sourceNetwork, destinationNetwork);
	Random* random = node_getRandom(worker_getPrivate()->cached_node);
	gdouble chance = random_nextDouble(random);
	if(chance > reliability){
		/* sender side is scheduling packets, but we are simulating
		 * the packet being dropped between sender and receiver, so
		 * it will need to be retransmitted */
		network_scheduleRetransmit(sourceNetwork, packet);
	} else {
		/* packet will make it through, find latency */
		gdouble latency = network_sampleLinkLatency(sourceNetwork, destinationNetwork);
		SimulationTime delay = (SimulationTime) floor(latency * SIMTIME_ONE_MILLISECOND);

		PacketArrivedEvent* event = packetarrived_new(packet);
		worker_scheduleEvent((Event*)event, delay, (GQuark)destinationIP);
	}
}
