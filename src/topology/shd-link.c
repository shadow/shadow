/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
