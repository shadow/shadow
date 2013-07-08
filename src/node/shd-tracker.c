/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _TrackerFlags TrackerFlags;
enum _TrackerFlags {
	TRACKER_FLAGS_NONE = 0,
	TRACKER_FLAGS_NODE = 1<<0,
	TRACKER_FLAGS_SOCKET = 1<<1,
	TRACKER_FLAGS_RAM = 1<<2,
};

struct _Tracker {
	SimulationTime interval;
	GLogLevelFlags loglevel;
	TrackerFlags flags;

	gboolean didLogNodeHeader;
	gboolean didLogRAMHeader;
	gboolean didLogSocketHeader;

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
	guint numFailedFrees;

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

static TrackerFlags _tracker_parseFlagString(gchar* flagString) {
	TrackerFlags flags = TRACKER_FLAGS_NONE;
	if(flagString) {
		/* info string can either be comma or space separated */
		gchar** parts = g_strsplit_set(flagString, " ,", -1);
		for(gint idx = 0; parts[idx]; idx++) {
			if(!g_ascii_strcasecmp(parts[idx], "node")) {
				flags |= TRACKER_FLAGS_NODE;
			} else if(!g_ascii_strcasecmp(parts[idx], "socket")) {
				flags |= TRACKER_FLAGS_SOCKET;
			} else if(!g_ascii_strcasecmp(parts[idx], "ram")) {
				flags |= TRACKER_FLAGS_RAM;
			} else {
				logging_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __FUNCTION__,
						"Did not recognize log info '%s', possible choices are 'node','socket'.", parts[idx]);
			}
		}
		g_strfreev(parts);
	}

	return flags;
}

static GLogLevelFlags _tracker_getLogLevel(Tracker* tracker) {
	/* prefer our level over the global config */
	GLogLevelFlags level = tracker->loglevel;
	if(!level) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			level = configuration_getHeartbeatLogLevel(c);
		}
	}
	return level;
}

static SimulationTime _tracker_getLogInterval(Tracker* tracker) {
	/* prefer our interval over the global config */
	SimulationTime interval = tracker->interval;
	if(!interval) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			interval = configuration_getHearbeatInterval(c);
		}
	}
	return interval;
}

static TrackerFlags _tracker_getFlags(Tracker* tracker) {
	/* prefer our log info over the global config */
	TrackerFlags flags = tracker->flags;
	if(!flags) {
		Worker* w = worker_getPrivate();
		if(w->cached_engine) {
			Configuration* c = engine_getConfig(w->cached_engine);
			flags = _tracker_parseFlagString(c->heartbeatLogInfo);
		}
	}
	return flags;
}

Tracker* tracker_new(SimulationTime interval, GLogLevelFlags loglevel, gchar* flagString) {
	Tracker* tracker = g_new0(Tracker, 1);
	MAGIC_INIT(tracker);

	tracker->interval = interval;
	tracker->loglevel = loglevel;
	tracker->flags = _tracker_parseFlagString(flagString);

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

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
		tracker->processingTimeTotal += processingTime;
		tracker->processingTimeLastInterval += processingTime;
	}
}

void tracker_addVirtualProcessingDelay(Tracker* tracker, SimulationTime delay) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
		(tracker->numDelayedTotal)++;
		tracker->delayTimeTotal += delay;
		(tracker->numDelayedLastInterval)++;
		tracker->delayTimeLastInterval += delay;
	}
}

void tracker_addInputBytes(Tracker* tracker, gsize inputBytes) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
		tracker->inputBytesTotal += inputBytes;
		tracker->inputBytesLastInterval += inputBytes;
	}
}

void tracker_addOutputBytes(Tracker* tracker, gsize outputBytes) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_NODE) {
		tracker->outputBytesTotal += outputBytes;
		tracker->outputBytesLastInterval += outputBytes;
	}
}

void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_RAM) {
		tracker->allocatedBytesTotal += allocatedBytes;
		tracker->allocatedBytesLastInterval += allocatedBytes;
		g_hash_table_insert(tracker->allocatedLocations, location, GSIZE_TO_POINTER(allocatedBytes));
	}
}

void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_RAM) {
		gpointer value = NULL;
		gboolean exists = g_hash_table_lookup_extended(tracker->allocatedLocations, location, NULL, &value);
		if(exists) {
			g_assert(g_hash_table_remove(tracker->allocatedLocations, location));
			gsize allocatedBytes = GPOINTER_TO_SIZE(value);
			tracker->allocatedBytesTotal -= allocatedBytes;
			tracker->deallocatedBytesLastInterval += allocatedBytes;
		} else {
			(tracker->numFailedFrees)++;
		}
	}
}

void tracker_addSocket(Tracker* tracker, gint handle, gsize inputBufferSize, gsize outputBufferSize) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
		TrackerSocket* socket = g_new0(TrackerSocket, 1);
		socket->handle = handle;
		socket->inputBufferSize = inputBufferSize;
		socket->outputBufferSize = outputBufferSize;

		g_hash_table_insert(tracker->sockets, &(socket->handle), socket);
	}
}

void tracker_updateSocketPeer(Tracker* tracker, gint handle, in_addr_t peerIP, in_port_t peerPort) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
		TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
		if(socket) {
			socket->peerIP = peerIP;
			socket->peerPort = peerPort;

			Internetwork* internetwork = worker_getInternet();
			socket->peerHostname = g_strdup(internetwork_resolveIP(internetwork, peerIP));
		}
	}
}

void tracker_updateSocketInputBuffer(Tracker* tracker, gint handle, gsize inputBufferLength, gsize inputBufferSize) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
		TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
		if(socket) {
			socket->inputBufferLength = inputBufferLength;
			socket->inputBufferSize = inputBufferSize;
		}
	}
}

void tracker_updateSocketOutputBuffer(Tracker* tracker, gint handle, gsize outputBufferLength, gsize outputBufferSize) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
		TrackerSocket* socket = g_hash_table_lookup(tracker->sockets, &handle);
		if(socket) {
			socket->outputBufferLength = outputBufferLength;
			socket->outputBufferSize = outputBufferSize;
		}
	}
}

void tracker_removeSocket(Tracker* tracker, gint handle) {
	MAGIC_ASSERT(tracker);

	if(_tracker_getFlags(tracker) & TRACKER_FLAGS_SOCKET) {
		g_hash_table_remove(tracker->sockets, &handle);
	}
}

static void _tracker_logNode(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
	guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
	gdouble cpuutil = (gdouble)(((gdouble)tracker->processingTimeLastInterval) / ((gdouble)interval));
	gdouble avgdelayms = 0.0;

	if(tracker->numDelayedLastInterval > 0) {
		gdouble delayms = (gdouble) (((gdouble)tracker->delayTimeLastInterval) / ((gdouble)SIMTIME_ONE_MILLISECOND));
		avgdelayms = (gdouble) (delayms / ((gdouble) tracker->numDelayedLastInterval));
	}

	if(!tracker->didLogNodeHeader) {
		tracker->didLogNodeHeader = TRUE;
		logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
				"[shadow-heartbeat] [node-header] interval-seconds,rx-bytes,tx-bytes,cpu-percent,delayed-count,avgdelay-milliseconds");
	}

	logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
		"[shadow-heartbeat] [node] %u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%f,%"G_GSIZE_FORMAT",%f",
		seconds, tracker->inputBytesLastInterval, tracker->outputBytesLastInterval, cpuutil, tracker->numDelayedLastInterval, avgdelayms);
}

static void _tracker_logSocket(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
	if(!tracker->didLogSocketHeader) {
		tracker->didLogSocketHeader = TRUE;
		logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
				"[shadow-heartbeat] [socket-header] descriptor-number,hostname:port-peer,inbuflen:bytes,inbufsize:bytes,outbuflen:bytes,outbufsize:bytes;...");
	}

	GList* socketList = g_hash_table_get_values(tracker->sockets);
	gint numSockets = 0;
	if(socketList) {
		GString* msg = g_string_new("[shadow-heartbeat] [socket] ");
		/* loop through all sockets we have in the hash table to log */
		for(GList* iter = g_list_first(socketList); iter; iter = g_list_next(iter)) {
			TrackerSocket* socket = (TrackerSocket* )iter->data;
			/* don't log sockets that don't have peer IP/port set */
			if(socket->peerIP) {
				g_string_append_printf(msg, "%d,%s:%d,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT";",
						socket->handle, /*inet_ntoa((struct in_addr){socket->peerIP})*/ socket->peerHostname, socket->peerPort,
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

static void _tracker_logRAM(Tracker* tracker, GLogLevelFlags level, SimulationTime interval) {
	guint seconds = (guint) (interval / SIMTIME_ONE_SECOND);
	guint numptrs = g_hash_table_size(tracker->allocatedLocations);

	if(!tracker->didLogRAMHeader) {
		tracker->didLogRAMHeader = TRUE;
		logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
				"[shadow-heartbeat] [ram-header] interval-seconds,alloc-bytes,dealloc-bytes,total-bytes,pointers-count,failfree-count");
	}

	logging_log(G_LOG_DOMAIN, level, __FUNCTION__,
		"[shadow-heartbeat] [ram] %u,%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%"G_GSIZE_FORMAT",%u,%u",
		seconds, tracker->allocatedBytesLastInterval, tracker->deallocatedBytesLastInterval,
		tracker->allocatedBytesTotal, numptrs, tracker->numFailedFrees);
}

void tracker_heartbeat(Tracker* tracker) {
	MAGIC_ASSERT(tracker);

	TrackerFlags flags = _tracker_getFlags(tracker);
	GLogLevelFlags level = _tracker_getLogLevel(tracker);
	SimulationTime interval = _tracker_getLogInterval(tracker);

	/* check to see if node info is being logged */
	if(flags & TRACKER_FLAGS_NODE) {
		_tracker_logNode(tracker, level, interval);
	}

	/* check to see if socket buffer info is being logged */
	if(flags & TRACKER_FLAGS_SOCKET) {
		_tracker_logSocket(tracker, level, interval);
	}

	/* check to see if ram info is being logged */
	if(flags & TRACKER_FLAGS_RAM) {
		_tracker_logRAM(tracker, level, interval);
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
