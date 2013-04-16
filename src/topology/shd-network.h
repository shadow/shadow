/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
 * @param link
 */
void network_addLink(Network* network, gpointer link); /* XXX: type is "Link*" */

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @return
 */
gdouble network_getLinkReliability(in_addr_t sourceIP, in_addr_t destinationIP);

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @param percentile
 * @return
 */
gdouble network_getLinkLatency(in_addr_t sourceIP, in_addr_t destinationIP, gdouble percentile);

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @return
 */
gdouble network_sampleLinkLatency(in_addr_t sourceIP, in_addr_t destinationIP);

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
