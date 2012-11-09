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
