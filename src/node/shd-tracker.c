/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _TrackerLogInfo TrackerLogInfo;
enum _TrackerLogInfo {
	TRACKER_INFO_NONE = 0,
	TRACKER_INFO_NODE = 1<<0,
	TRACKER_INFO_SOCKET = 1<<1,
};


struct _Tracker {
	SimulationTime interval;
	GLogLevelFlags loglevel;
	TrackerLogInfo loginfo;

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

	GHashTable* sockets;

	SimulationTime lastHeartbeat;

	MAGIC_DECLARE;
};

typedef struct _TrackerSocket TrackerSocket;
struct _TrackerSocket {
	gint handle;
	in_addr_t peerIP;
	in_port_t peerPort;
	gchar* peerHostname;
	gsize inputBufferSize;
	gsize inputBufferLength;
	gsize outputBufferSize;
	gsize outputBufferLength;

	MAGIC_DECLARE;
};

TrackerLogInfo _tracker_getLogInfo(gchar* info) {
	TrackerLogInfo loginfo = TRACKER_INFO_NONE;
	if(info) {
		/* info string can either be comma or space separated */
		gchar** parts = g_strsplit_set(info, " ,", -1);
		for(gint idx = 0; parts[idx]; idx++) {
			if(!g_ascii_strcasecmp(parts[idx], "node")) {
				loginfo |= TRACKER_INFO_NODE;
			} else if(!g_ascii_strcasecmp(parts[idx], "socket")) {
				loginfo |= TRACKER_INFO_SOCKET;
			} else {
				logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __FUNCTION__,
						"Did not recognize log info '%s', possible choices are 'node','socket'.", parts[idx]);
			}
		}
		g_strfreev(parts);
	}

	return loginfo;
}

Tracker* tracker_new(SimulationTime interval, GLogLevelFlags loglevel, gchar* loginfo) {
	Tracker* tracker = g_new0(Tracker, 1);
	MAGIC_INIT(tracker);

	tracker->interval = interval;
	tracker->loglevel = loglevel;
	tracker->loginfo = _tracker_getLogInfo(loginfo);

	tracker->allocatedLocations = g_hash_table_new(g_direct_hash, g_direct_equal);
	tracker->sockets = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

	return tracker;
}

void tracker_free(Tracker* tracker) {
	MAGIC_ASSERT(tracker);

	g_hash_table_destroy(tracker->allocatedLocations);
	g_hash_table_destroy(tracker->sockets);

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

void tracker_addSocket(Tracker* tracker, gint handle, gsize inputBufferSize, gsize outputBufferSize) {
	MAGIC_ASSERT(tracker);

	TrackerSocket* socket = g_new0(TrackerSocket, 1);
	socket->handle = handle;
	socket->inputBufferSize = inputBufferSize;
	socket->outputBufferSize = outputBufferSize;

	g_hash_table_insert(tracker->sockets, &(socket->handle), socket);
}

void tracker_updateSocketPeer(Tracker* tracker, gint handle, in_addr_t peerIP, in_port_t peerPort) {
	MAGIC_ASSERT(tracker);

	TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
	if(socket) {
		socket->peerIP = peerIP;
		socket->peerPort = peerPort;

		Internetwork* internetwork = worker_getInternet();
		socket->peerHostname = g_strdup(internetwork_resolveIP(internetwork, peerIP));
	}
}

void tracker_updateSocketInputBuffer(Tracker* tracker, gint handle, gsize inputBufferLength, gsize inputBufferSize) {
	MAGIC_ASSERT(tracker);

	TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
	if(socket) {
		socket->inputBufferLength = inputBufferLength;
		socket->inputBufferSize = inputBufferSize;
	}
}

void tracker_updateSocketOutputBuffer(Tracker* tracker, gint handle, gsize outputBufferLength, gsize outputBufferSize) {
	MAGIC_ASSERT(tracker);

	TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
	if(socket) {
		socket->outputBufferLength = outputBufferLength;
		socket->outputBufferSize = outputBufferSize;
	}
}

void tracker_removeSocket(Tracker* tracker, gint handle) {
	MAGIC_ASSERT(tracker);
	g_hash_table_remove(tracker->sockets, &handle);
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

	/* prefer our log info over the global config */
	TrackerLogInfo loginfo = tracker->loginfo;
	if(!loginfo) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			loginfo = _tracker_getLogInfo(c->heartbeatLogInfo);
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

	/* check to see if node info is being logged */
	if(loginfo & TRACKER_INFO_NODE) {
		logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
				"[shadow-heartbeat] [node] CPU %f \%, MEM %f KiB, interval %u seconds, alloc %f KiB, dealloc %f KiB, Rx %f B, Tx %f B, avgdelay %f milliseconds",
				cpuutil, mem, seconds, alloc, dealloc, in, out, avedelayms);
	}

	/* check to see if socket buffer info is being logged */
	if(loginfo & TRACKER_INFO_SOCKET) {
		GList* socketList = g_hash_table_get_values(tracker->sockets);
		gint numSockets = 0;
		if(socketList) {
			GString* msg = g_string_new("[shadow-heartbeat] [socket] ");
			/* loop through all sockets we have in the hash table to log */
			for(GList* iter = g_list_first(socketList); iter; iter = g_list_next(iter)) {
				TrackerSocket* socket = (TrackerSocket* )iter->data;
				/* don't log sockets that don't have peer IP/port set */
				if(socket->peerIP) {
					g_string_append_printf(msg, "%d,%s:%d,%lu,%lu,%lu,%lu;", socket->handle, /*inet_ntoa((struct in_addr){socket->peerIP})*/ socket->peerHostname, socket->peerPort,
							socket->inputBufferLength, socket->inputBufferSize,	socket->outputBufferLength, socket->outputBufferSize);
					numSockets++;
				}
			}

			if(numSockets > 0) {
				logging_log(G_LOG_DOMAIN, level, __FUNCTION__, "%s", msg->str);
			}
			g_string_free(msg, TRUE);
		}
	}


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
