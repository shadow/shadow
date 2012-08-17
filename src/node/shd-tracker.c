/**
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

struct _Tracker {
	SimulationTime interval;
	GLogLevelFlags loglevel;

	SimulationTime processingTimeTotal;
	SimulationTime processingTimeLastInterval;

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

	/* log the things we are tracking */
	logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
			"shadow-heartbeat: CPU %f \%, MEM %f KiB, interval %u seconds, alloc %f KiB, dealloc %f KiB, Rx %f B, Tx %f B",
			cpuutil, mem, seconds, alloc, dealloc, in, out);

	/* clear interval stats */
	tracker->processingTimeLastInterval = 0;
	tracker->inputBytesLastInterval = 0;
	tracker->outputBytesLastInterval = 0;
	tracker->allocatedBytesLastInterval = 0;
	tracker->deallocatedBytesLastInterval = 0;

	/* schedule the next heartbeat */
	tracker->lastHeartbeat = worker_getPrivate()->clock_now;
	HeartbeatEvent* heartbeat = heartbeat_new(tracker);
	worker_scheduleEvent((Event*)heartbeat, interval, 0);
}
