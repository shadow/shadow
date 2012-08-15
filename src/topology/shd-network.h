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

/**
 *
 */
typedef struct _Network Network;

/**
 *
 * @param id
 * @param bandwidthdown
 * @param bandwidthup
 * @return
 */
Network* network_new(GQuark id, guint64 bandwidthdown, guint64 bandwidthup, gdouble packetloss);

/**
 *
 * @param data
 */
void network_free(gpointer data);

/**
 *
 * @param network
 * @return
 */
GQuark* network_getIDReference(Network* network);

/**
 *
 * @param network
 * @return
 */
guint64 network_getBandwidthUp(Network* network);

/**
 *
 * @param network
 * @return
 */
guint64 network_getBandwidthDown(Network* network);

/**
 *
 * @param a
 * @param b
 * @param user_data
 * @return
 */
gint network_compare(gconstpointer a, gconstpointer b, gpointer user_data);

/**
 *
 * @param a
 * @param b
 * @return
 */
gboolean network_isEqual(Network* a, Network* b);

/**
 *
 * @param network
 * @param outgoingLink
 */
void network_addOutgoingLink(Network* network, gpointer outgoingLink); /* XXX: type is "Link*" */

/**
 *
 * @param network
 * @param incomingLink
 */
void network_addIncomingLink(Network* network, gpointer incomingLink); /* XXX: type is "Link*" */

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @return
 */
gdouble network_getLinkReliability(Network* sourceNetwork, Network* destinationNetwork);

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @param percentile
 * @return
 */
gdouble network_getLinkLatency(Network* sourceNetwork, Network* destinationNetwork, gdouble percentile);

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @return
 */
gdouble network_sampleLinkLatency(Network* sourceNetwork, Network* destinationNetwork);

/**
 *
 * @param sourceNetwork
 * @param packet
 */
void network_schedulePacket(Network* sourceNetwork, Packet* packet);

/**
 *
 * @param network
 * @param packet
 */
void network_scheduleRetransmit(Network* network, Packet* packet);

#endif /* SHD_NETWORK_H_ */
