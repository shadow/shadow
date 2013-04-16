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

#ifndef SHD_LINK_H_
#define SHD_LINK_H_

#include "shadow.h"

/* a directed link between two networks */

/**
 *
 */
typedef struct _Link Link;

/**
 *
 * @param sourceNetwork
 * @param destinationNetwork
 * @param latency
 * @param jitter
 * @param packetloss
 * @return
 */
Link* link_new(Network* sourceNetwork, Network* destinationNetwork, guint64 latency,
		guint64 jitter, gdouble packetloss, guint64 latencymin, guint64 latencyQ1,
		guint64 latencymean, guint64 latencyQ3, guint64 latencymax);

/**
 *
 * @param data
 */
void link_free(gpointer data);

/**
 *
 * @param link
 * @return
 */
Network* link_getSourceNetwork(Link* link);

/**
 *
 * @param link
 * @return
 */
Network* link_getDestinationNetwork(Link* link);

/**
 *
 * @param link
 * @return
 */
guint64 link_getLatency(Link* link);

/**
 *
 * @param link
 * @return
 */
guint64 link_getJitter(Link* link);

/**
 *
 * @param link
 * @return
 */
gdouble link_getPacketLoss(Link* link);

void link_getLatencyMetrics(Link *link, guint64 *min, guint64 *q1, guint64 *mean, guint64 *q3, guint64 *max);

/**
 *
 * @param link
 * @param percentile
 * @return
 */
guint64 link_computeDelay(Link* link, gdouble percentile);

#endif /* SHD_LINK_H_ */
