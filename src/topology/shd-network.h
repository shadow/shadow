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

#ifndef SHD_NETWORK_H_
#define SHD_NETWORK_H_

#include "shadow.h"

/* FIXME: forward declaration to avoid circular dependencies... */
typedef struct _Link Link;

typedef struct _Network Network;

struct _Network {
	GQuark id;
	/* links to other networks this network can access */
	GList* outgoingLinks;
	/* links from other networks that can access this network */
	GList* incomingLinks;
	/* map to outgoing links by network id */
	GHashTable* outgoingLinkMap;
	MAGIC_DECLARE;
};

Network* network_new(GQuark id);
void network_free(gpointer data);

gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean network_isEqual(Network* a, Network* b);

void network_addOutgoingLink(Network* network, Link* outgoingLink);
void network_addIncomingLink(Network* network, Link* incomingLink);

gdouble network_getLinkReliability(Network* sourceNetwork, Network* destinationNetwork);
gdouble network_getLinkLatency(Network* sourceNetwork, Network* destinationNetwork, gdouble percentile);
gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork);

// TODO these are out of place...
void network_scheduleClose(GQuark callerID, GQuark sourceID, in_port_t sourcePort,
		GQuark destinationID, in_port_t destinationPort, guint32 receiveEnd);
void network_scheduleRetransmit(rc_vpacket_pod_tp rc_packet, GQuark callerID);
void network_schedulePacket(rc_vpacket_pod_tp rc_packet);

#endif /* SHD_NETWORK_H_ */
