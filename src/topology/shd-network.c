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
		return G_MINDOUBLE;
	}
}

gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork) {
	MAGIC_ASSERT(sourceNetwork);
	MAGIC_ASSERT(destinationNetwork);
	Link* link = g_hash_table_lookup(sourceNetwork->outgoingLinkMap, &(destinationNetwork->id));
	if(link) {
		return cdf_getRandomValue(link->latency);
	}
	return G_MINDOUBLE;
}

void network_scheduleClose(GQuark callerID, GQuark sourceID, in_port_t sourcePort,
		GQuark destinationID, in_port_t destinationPort, guint32 receiveEnd)
{
	/* TODO refactor - this was hacked to allow loopback addresses */
	SimulationTime delay = 0;

	if(sourceID == htonl(INADDR_LOOPBACK) || destinationID == htonl(INADDR_LOOPBACK)) {
		/* going to loopback, virtually no delay */
		delay = 1;
	} else {
		Worker* worker = worker_getPrivate();
		Internetwork* internet = worker->cached_engine->internet;
		gdouble latency = internetwork_sampleLatency(internet, sourceID, destinationID);
		delay = (SimulationTime) (latency * SIMTIME_ONE_MILLISECOND);
	}

	/* deliver to dst_addr, the other end of the conenction. if that is 127.0.0.1,
	 * then use caller addr so we can do the node lookup.
	 */
	GQuark deliverID = 0;
	if(destinationID == htonl(INADDR_LOOPBACK)) {
		deliverID = callerID;
	} else {
		deliverID = destinationID;
	}

	TCPCloseTimerExpiredEvent* event = tcpclosetimerexpired_new(callerID, sourceID, sourcePort, destinationID, destinationPort, receiveEnd);
	worker_scheduleEvent((Event*)event, delay, deliverID);
}

void network_scheduleRetransmit(rc_vpacket_pod_tp rc_packet, GQuark callerID) {
	/* TODO refactor - this was hacked to allow loopback addresses */
	rc_vpacket_pod_retain_stack(rc_packet);
	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		goto ret;
	}

	SimulationTime delay;
	if(packet->header.source_addr == htonl(INADDR_LOOPBACK)) {
		/* going to loopback, virtually no delay */
		delay = 1;
	} else {
		/* source should retransmit.
		 * retransmit timers depend on RTT, use latency as approximation since in most
		 * cases the dest will be dropping a packet and one latency has already been incurred. */
		Worker* worker = worker_getPrivate();
		Internetwork* internet = worker->cached_engine->internet;
		gdouble latency = internetwork_sampleLatency(internet,
				(GQuark)packet->header.source_addr, (GQuark)packet->header.destination_addr);
		delay = (SimulationTime) (latency * SIMTIME_ONE_MILLISECOND);
	}


	/* deliver to src_addr, the other end of the conenction. if that is 127.0.0.1,
	 * then use caller addr so we can do the node lookup.
	 */
	GQuark deliverID = 0;
	if(packet->header.source_addr == htonl(INADDR_LOOPBACK)) {
		deliverID = callerID;
	} else {
		deliverID = (GQuark) packet->header.source_addr;
	}

	TCPRetransmitTimerExpiredEvent* event = tcpretransmittimerexpired_new(callerID,
			(GQuark)packet->header.source_addr, packet->header.source_port,
			(GQuark)packet->header.destination_addr, packet->header.destination_port,
			packet->tcp_header.sequence_number);
	worker_scheduleEvent((Event*)event, delay, deliverID);

	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
ret:
	rc_vpacket_pod_release_stack(rc_packet);
}

void network_schedulePacket(rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	gint do_unlock = 1;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		error("packet is NULL!");
		do_unlock = 0;
		goto ret;
	}

	GQuark sourceID = (GQuark) packet->header.source_addr;
	GQuark destinationID = (GQuark) packet->header.destination_addr;

	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

	Worker* worker = worker_getPrivate();
	Internetwork* internet = worker->cached_engine->internet;

	/* first thing to check is if network reliability forces us to 'drop'
	 * the packet. if so, get out of dodge doing as little as possible.
	 */
	gdouble reliability = internetwork_getReliability(internet, sourceID, destinationID);
	if(dvn_rand_unit() > reliability){
		/* sender side is scheduling packets, but we are simulating
		 * the packet being dropped between sender and receiver, so
		 * it will need to be retransmitted */
		network_scheduleRetransmit(rc_packet, sourceID);
		goto ret;
	}

	/* packet will make it through, find latency */
	gdouble latency = internetwork_sampleLatency(internet, sourceID, destinationID);

	PacketArrivedEvent* event = packetarrived_new(rc_packet);
	worker_scheduleEvent((Event*)event, latency, (GQuark) packet->header.destination_addr);

ret:
	if(do_unlock) {
		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);
	}
	rc_vpacket_pod_release_stack(rc_packet);
}
