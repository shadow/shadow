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

Network* network_new(GQuark id) {
	Network* network = g_new0(Network, 1);
	MAGIC_INIT(network);

	network->id = id;

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

void network_addOutgoingLink(Network* network, Link* outgoingLink) {
	MAGIC_ASSERT(network);

	/* prepending is O(1), but appending is O(n) since it traverses the list */
	network->outgoingLinks = g_list_prepend(network->outgoingLinks, outgoingLink);

	/* FIXME: if this link replaces an existing, the existing will be freed in
	 * the replace call but still exist in the list of outgoing links (and the
	 * list of incoming links at the other network)
	 * this is because we currently only support single links to each network
	 */
	Network* destination = link_getDestinationNetwork(outgoingLink);
	g_hash_table_replace(network->outgoingLinkMap, &(destination->id), outgoingLink);
}

void network_addIncomingLink(Network* network, Link* incomingLink) {
	MAGIC_ASSERT(network);

	/* prepending is O(1), but appending is O(n) since it traverses the list */
	network->incomingLinks = g_list_prepend(network->incomingLinks, incomingLink);
}

gdouble network_getLinkReliability(Network* sourceNetwork, Network* destinationNetwork) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);
	Link* link = g_hash_table_lookup(sourceNetwork->outgoingLinkMap, &(destinationNetwork->id));
	if(link) {
		return link->reliability;
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
		return cdf_getValue(link->latency, percentile);
	} else {
		critical("unable to find link between networks '%s' and '%s'. Check XML file for errors.",
				g_quark_to_string(sourceNetwork->id), g_quark_to_string(destinationNetwork->id));
		return G_MAXDOUBLE;
	}
}

gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);
	Link* link = g_hash_table_lookup(sourceNetwork->outgoingLinkMap, &(destinationNetwork->id));
	if(link) {
		return cdf_getRandomValue(link->latency);
	} else {
		critical("unable to find link between networks '%s' and '%s'. Check XML file for errors.",
				g_quark_to_string(sourceNetwork->id), g_quark_to_string(destinationNetwork->id));
		return G_MAXDOUBLE;
	}
}

void network_scheduleClose(GQuark callerID, GQuark sourceID, in_port_t sourcePort,
		GQuark destinationID, in_port_t destinationPort, guint32 receiveEnd)
{
	/* TODO refactor - this was hacked to allow loopback addresses */
//	SimulationTime delay = 0;
//
//	if(sourceID == htonl(INADDR_LOOPBACK) || destinationID == htonl(INADDR_LOOPBACK)) {
//		/* going to loopback, virtually no delay */
//		delay = 1;
//	} else {
//		Worker* worker = worker_getPrivate();
//		Internetwork* internet = worker->cached_engine->internet;
//		gdouble latency = internetwork_sampleLatency(internet, sourceID, destinationID);
//		delay = (SimulationTime) (latency * SIMTIME_ONE_MILLISECOND);
//	}
//
//	/* deliver to dst_addr, the other end of the conenction. if that is 127.0.0.1,
//	 * then use caller addr so we can do the node lookup.
//	 */
//	GQuark deliverID = 0;
//	if(destinationID == htonl(INADDR_LOOPBACK)) {
//		deliverID = callerID;
//	} else {
//		deliverID = destinationID;
//	}
//
//	TCPCloseTimerExpiredEvent* event = tcpclosetimerexpired_new(callerID, sourceID, sourcePort, destinationID, destinationPort, receiveEnd);
//	worker_scheduleEvent((Event*)event, delay, deliverID);
}

void network_scheduleRetransmit(Network* network, Packet* packet) {
	MAGIC_ASSERT(network);

	// FIXME make sure loopback packets dont get here!!

	/* source should retransmit. use latency to approximate RTT for 'retransmit timer' */
	Worker* worker = worker_getPrivate();
	Internetwork* internet = worker->cached_engine->internet;

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

	Worker* worker = worker_getPrivate();
	Internetwork* internet = worker->cached_engine->internet;
	in_addr_t destinationIP = packet_getDestinationIP(packet);
	Network* destinationNetwork = internetwork_lookupNetwork(internet, destinationIP);

	/* first thing to check is if network reliability forces us to 'drop'
	 * the packet. if so, get out of dodge doing as little as possible.
	 */
	gdouble reliability = network_getLinkReliability(sourceNetwork, destinationNetwork);
	if(dvn_rand_unit() > reliability){
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
