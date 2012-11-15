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

struct _Link {
	Network* sourceNetwork;
	Network* destinationNetwork;
	guint64 latency;
	guint64 jitter;
	gdouble packetloss;
	guint64 latencymin;
	guint64 latencyQ1;
	guint64 latencymean;
	guint64 latencyQ3;
	guint64 latencymax;
	MAGIC_DECLARE;
};

Link* link_new(Network* sourceNetwork, Network* destinationNetwork, guint64 latency,
		guint64 jitter, gdouble packetloss, guint64 latencymin, guint64 latencyQ1,
		guint64 latencymean, guint64 latencyQ3, guint64 latencymax) {
	Link* link = g_new0(Link, 1);
	MAGIC_INIT(link);

	link->sourceNetwork = sourceNetwork;
	link->destinationNetwork = destinationNetwork;
	link->latency = latency;
	link->jitter = jitter;
	link->packetloss = packetloss;

	link->latencymin = latencymin;
	link->latencyQ1 = latencyQ1;
	link->latencymean = latencymean;
	link->latencyQ3 = latencyQ3;
	link->latencymax = latencymax;

	return link;
}


void link_free(gpointer data) {
	Link* link = data;
	MAGIC_ASSERT(link);

	MAGIC_CLEAR(link);
	g_free(link);
}

Network* link_getSourceNetwork(Link* link) {
	MAGIC_ASSERT(link);
	return link->sourceNetwork;
}

Network* link_getDestinationNetwork(Link* link) {
	MAGIC_ASSERT(link);
	return link->destinationNetwork;
}

guint64 link_getLatency(Link* link) {
	MAGIC_ASSERT(link);
	return link->latency;
}

guint64 link_getJitter(Link* link) {
	MAGIC_ASSERT(link);
	return link->jitter;
}

gdouble link_getPacketLoss(Link* link) {
	MAGIC_ASSERT(link);
	return link->packetloss;
}

guint64 link_computeDelay(Link* link, gdouble percentile) {
	MAGIC_ASSERT(link);
	g_assert((percentile >= 0) && (percentile <= 1));

	guint64 delay;
	if(link->latencymin == 0) {
		guint64 min = link->latency - link->jitter;
		guint64 max = link->latency + link->jitter;

		guint64 width = max - min;
		guint64 offset = (guint64)(((gdouble)width) * percentile);

		delay = min + offset;
	} else {
		guint64 min;
		guint64 width;
		gdouble r;
		if(percentile <= 0.25) {
			min = link->latencymin;
			width = link->latencyQ1 - link->latencymin;
			r = percentile / 0.25;
		} else if(percentile <= 0.5) {
			min = link->latencyQ1;
			width = link->latency - link->latencyQ1;
			r = (percentile - 0.25) / 0.25;
		} else if(percentile <= 0.75) {
			min = link->latency;
			width = link->latencyQ3 - link->latency;
			r = (percentile - 0.50) / 0.25;
		} else {
			min = link->latencyQ3;
			width = link->latencymax - link->latencyQ3;
			r = (percentile - 0.75) / 0.25;
		}

		delay = min + (width * r);
	}

	return delay;
}

void link_getLatencyMetrics(Link *link, guint64 *min, guint64 *q1, guint64 *mean, guint64 *q3, guint64 *max) {
	MAGIC_ASSERT(link);
	*min = link->latencymin;
	*q1 = link->latencyQ1;
	*mean = link->latencymean;
	*q3 = link->latencyQ3;
	*max = link->latencymax;
}
