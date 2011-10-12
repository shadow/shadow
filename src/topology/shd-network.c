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

Network* network_new(GQuark id, CumulativeDistribution* intranetLatency) {
	Network* network = g_new0(Network, 1);
	MAGIC_INIT(network);

	network->id = id;
	network->intranetLatency = intranetLatency;

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

gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Network* na = a;
	const Network* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->id > nb->id ? +1 : na->id == nb->id ? 0 : -1;
}

gboolean network_equal(Network* a, Network* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		return network_compare(a, b, NULL) == 0;
	}
}
