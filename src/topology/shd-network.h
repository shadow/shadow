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

#ifndef SHD_NETWORK_H_
#define SHD_NETWORK_H_

#include "shadow.h"

/* FIXME: forward declaration to avoid circular dependencies... */
typedef struct _Link Link;

typedef struct _Network Network;

Network* network_new(GQuark id, guint64 bandwidthdown, guint64 bandwidthup);
void network_free(gpointer data);

GQuark* network_getIDReference(Network* network);
guint64 network_getBandwidthUp(Network* network);
guint64 network_getBandwidthDown(Network* network);

gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean network_isEqual(Network* a, Network* b);

void network_addOutgoingLink(Network* network, Link* outgoingLink);
void network_addIncomingLink(Network* network, Link* incomingLink);

gdouble network_getLinkReliability(Network* sourceNetwork, Network* destinationNetwork);
gdouble network_getLinkLatency(Network* sourceNetwork, Network* destinationNetwork, gdouble percentile);
gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork);

void network_schedulePacket(Network* sourceNetwork, Packet* packet);
void network_scheduleRetransmit(Network* network, Packet* packet);

// TODO these are out of place...
void network_scheduleClose(GQuark callerID, GQuark sourceID, in_port_t sourcePort,
		GQuark destinationID, in_port_t destinationPort, guint32 receiveEnd);

#endif /* SHD_NETWORK_H_ */
