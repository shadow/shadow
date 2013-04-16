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

#include "shadow.h"

struct _Tracker {
	SimulationTime interval;
	GLogLevelFlags loglevel;

	SimulationTime processingTimeTotal;
	SimulationTime processingTimeLastInterval;

	gsize numDelayedTotal;
	SimulationTime delayTimeTotal;
	gsize numDelayedLastInterval;
	SimulationTime delayTimeLastInterval;

	gsize inputBytesTotal;
	gsize inputBytesLastInterval;

	gsize outputBytesTotal;
	gsize outputBytesLastInterval;

	GHashTable* allocatedLocations;
	gsize allocatedBytesTotal;
	gsize allocatedBytesLastInterval;
	gsize deallocatedBytesLastInterval;

	SimulationTime lastHeartbeat;

	MAGIC_DECLARE;
};

Tracker* tracker_new(SimulationTime interval, GLogLevelFlags loglevel) {
	Tracker* tracker = g_new0(Tracker, 1);
	MAGIC_INIT(tracker);

	tracker->interval = interval;
	tracker->loglevel = loglevel;
	tracker->allocatedLocations = g_hash_table_new(g_direct_hash, g_direct_equal);

	return tracker;
}

void tracker_free(Tracker* tracker) {
	MAGIC_ASSERT(tracker);

	g_hash_table_destroy(tracker->allocatedLocations);

	MAGIC_CLEAR(tracker);
	g_free(tracker);
}

void tracker_addProcessingTime(Tracker* tracker, SimulationTime processingTime) {
	MAGIC_ASSERT(tracker);
	tracker->processingTimeTotal += processingTime;
	tracker->processingTimeLastInterval += processingTime;
}

void tracker_addVirtualProcessingDelay(Tracker* tracker, SimulationTime delay) {
	MAGIC_ASSERT(tracker);
	(tracker->numDelayedTotal)++;
	tracker->delayTimeTotal += delay;
	(tracker->numDelayedLastInterval)++;
	tracker->delayTimeLastInterval += delay;
}

void tracker_addInputBytes(Tracker* tracker, gsize inputBytes) {
	MAGIC_ASSERT(tracker);
	tracker->inputBytesTotal += inputBytes;
	tracker->inputBytesLastInterval += inputBytes;
}

void tracker_addOutputBytes(Tracker* tracker, gsize outputBytes) {
	MAGIC_ASSERT(tracker);
	tracker->outputBytesTotal += outputBytes;
	tracker->outputBytesLastInterval += outputBytes;
}

void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes) {
	MAGIC_ASSERT(tracker);
	tracker->allocatedBytesTotal += allocatedBytes;
	tracker->allocatedBytesLastInterval += allocatedBytes;
	g_hash_table_insert(tracker->allocatedLocations, location, GSIZE_TO_POINTER(allocatedBytes));
}

void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location) {
	MAGIC_ASSERT(tracker);
	gpointer value = g_hash_table_lookup(tracker->allocatedLocations, location);
	if(value) {
		gsize allocatedBytes = GPOINTER_TO_SIZE(value);
		tracker->allocatedBytesTotal -= allocatedBytes;
		tracker->deallocatedBytesLastInterval += allocatedBytes;
	}
}

void tracker_heartbeat(Tracker* tracker) {
	MAGIC_ASSERT(tracker);

	/* prefer our level over the global config */
	GLogLevelFlags level = tracker->loglevel;
	if(!level) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			level = configuration_getHeartbeatLogLevel(c);
		}
	}

	/* prefer our interval over the global config */
	SimulationTime interval = tracker->interval;
	if(!interval) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			interval = configuration_getHearbeatInterval(c);
		}
	}

	guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);

	double in = (double) (tracker->inputBytesLastInterval);
	double out = (double)(tracker->outputBytesLastInterval);
	double alloc = (double)(((double)tracker->allocatedBytesLastInterval) / 1024.0);
	double dealloc = (double)(((double)tracker->deallocatedBytesLastInterval) / 1024.0);

	double mem = (double)(((double)tracker->allocatedBytesTotal) / 1024.0);
	double cpuutil = (double)(((double)tracker->processingTimeLastInterval) / interval);

	double avedelayms = 0.0;
	if(tracker->numDelayedLastInterval > 0) {
		double delayms = (double) (((double)tracker->delayTimeLastInterval) / ((double)SIMTIME_ONE_MILLISECOND));
		avedelayms = (double) (delayms / ((double) tracker->numDelayedLastInterval));
	}

	/* log the things we are tracking */
	logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
			"[shadow-heartbeat] CPU %f \%, MEM %f KiB, interval %u seconds, alloc %f KiB, dealloc %f KiB, Rx %f B, Tx %f B, avgdelay %f milliseconds",
			cpuutil, mem, seconds, alloc, dealloc, in, out, avedelayms);

	/* clear interval stats */
	tracker->processingTimeLastInterval = 0;
	tracker->delayTimeLastInterval = 0;
	tracker->numDelayedLastInterval = 0;
	tracker->inputBytesLastInterval = 0;
	tracker->outputBytesLastInterval = 0;
	tracker->allocatedBytesLastInterval = 0;
	tracker->deallocatedBytesLastInterval = 0;

	/* schedule the next heartbeat */
	tracker->lastHeartbeat = worker_getPrivate()->clock_now;
	HeartbeatEvent* heartbeat = heartbeat_new(tracker);
	worker_scheduleEvent((Event*)heartbeat, interval, 0);
}
