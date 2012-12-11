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

#include <glib.h>

#include "shd-torcontrol.h"
#include "shd-torcontrol-statistics.h"

struct _TorControlStatistics {
	ShadowLogFunc log;
	enum torcontrolstatistic_state currentState;
	enum torcontrolstatistic_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;

	GHashTable* circuits;
	GHashTable* streams;
};

typedef struct _ConnectionStats ConnectionStats;
struct _ConnectionStats {
};

typedef struct _CircuitStats CircuitStats;
struct _CircuitStats {
	gint circuitID;
	GDateTime* launchTime;
	GDateTime* openTime;
	GDateTime* closeTime;

	GString* path;
	enum torControl_circPurpose purpose;
	gboolean isInternal; /* not used for exit streams */
	gboolean isOneHop; /* onehop directory tunnels */

	guint totalReadBytes;
	guint totalWriteBytes;
};

typedef struct _StreamStats StreamStats;
struct _StreamStats {
	gint streamID;
	gint circuitID;
	GDateTime* openTime;
	GDateTime* closeTime;

	in_addr_t targetIP;
	in_port_t targetPort;
	gint purpose;

	guint totalReadBytes;
	guint totalWriteBytes;
};

static ConnectionStats* _connectionstats_new() {
	ConnectionStats* cs = g_new0(ConnectionStats, 1);

	return cs;
}

static void _connectionstats_free(ConnectionStats* cs) {
	g_assert(cs);

	g_free(cs);
}

static CircuitStats* _circuitstats_new(GDateTime* launchTime, gint circuitId,
		GString* path, enum torControl_circPurpose purpose, gboolean isInternal,
		gboolean isOneHop) {
	CircuitStats* cs = g_new0(CircuitStats, 1);

	cs->circuitID = circuitId;
	cs->purpose = purpose;
	cs->isInternal = isInternal;
	cs->isOneHop = isOneHop;

	cs->launchTime = launchTime;
	g_date_time_ref(launchTime);

	if(path) {
		cs->path = g_string_new(path->str);
	}

	return cs;
}

static void _circuitstats_free(CircuitStats* cs) {
	g_assert(cs);

	if (cs->launchTime) {
		g_date_time_unref(cs->launchTime);
	}
	if (cs->openTime) {
		g_date_time_unref(cs->openTime);
	}
	if (cs->closeTime) {
		g_date_time_unref(cs->closeTime);
	}

	if (cs->path) {
		g_string_free(cs->path, (cs->path->str ? TRUE : FALSE));
	}

	g_free(cs);
}

static StreamStats* _streamstats_new(gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint purpose) {
	StreamStats* ss = g_new0(StreamStats, 1);

	ss->streamID = streamID;
	ss->circuitID = circID;
	ss->targetIP = targetIP;
	ss->targetPort = targetPort;
	ss->purpose = purpose;

	return ss;
}

static void _streamstats_free(StreamStats* ss) {
	g_assert(ss);

	if (ss->openTime) {
		g_date_time_unref(ss->openTime);
	}
	if (ss->closeTime) {
		g_date_time_unref(ss->closeTime);
	}

	g_free(ss);
}

/*
 * setting up and registering with the ControlPort
 */

static gboolean _torcontrolstatistics_manageState(TorControlStatistics* tstats) {

	beginmanage: switch (tstats->currentState) {

	case TCS_SEND_AUTHENTICATE: {
		/* authenticate with the control port */
		if (torControl_authenticate(tstats->targetSockd, "password") > 0) {
			/* idle until we receive the response, then move to next state */
			tstats->currentState = TCS_IDLE;
			tstats->nextState = TCS_RECV_AUTHENTICATE;
		}
		break;
	}

	case TCS_RECV_AUTHENTICATE: {
		tstats->currentState = TCS_SEND_SETEVENTS;
		goto beginmanage;
		break;
	}

	case TCS_SEND_SETEVENTS: {
		/* send list of events to listen on */
		if (torControl_setevents(tstats->targetSockd, "CIRC STREAM ORCONN BW STREAM_BW")
				> 0) {
			/* idle until we receive the response, then move to next state */
			tstats->currentState = TCS_IDLE;
			tstats->nextState = TCS_RECV_SETEVENTS;
		}
		break;
	}

	case TCS_RECV_SETEVENTS: {
		/* all done */
		tstats->currentState = TCS_IDLE;
		tstats->nextState = TCS_IDLE;
		goto beginmanage;
		break;
	}

	case TCS_IDLE: {
		if (tstats->nextState == TCS_IDLE) {
			return TRUE;
		}
		break;
	}

	default:
		break;
	}

	return FALSE;
}

static void _torcontrolstatistics_handleResponseEvent(
		TorControlStatistics* tstats, GList *reply, gpointer userData) {
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

	switch (TORCTL_CODE_TYPE(replyLine->code)) {
	case TORCTL_REPLY_ERROR: {
		tstats->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s",
				replyLine->code, replyLine->body);
		break;
	}

	case TORCTL_REPLY_SUCCESS: {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s",
				replyLine->code, replyLine->body);
		tstats->currentState = tstats->nextState;
		_torcontrolstatistics_manageState(tstats);
		break;
	}

	default:
		break;
	}
}

/*
 * handling the asynchronous events from control port
 */

static void _torcontrolstatistics_handleORConnEvent(
		TorControlStatistics* tstats, gint code, gint connID, gchar *target, gint status,
		gint reason, gint numCircuits) {
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i ORCONN %i: id=%u target=%s status=%i reason=%i numcircs=%i",
			tstats->targetHostname->str, tstats->targetPort, code, connID, target,
			status, reason, numCircuits);

	if (status == TORCTL_ORCONN_STATUS_CONNECTED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s orconnection connected target %s",
				tstats->targetHostname->str, target);
	}

	if (status == TORCTL_ORCONN_STATUS_FAILED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s orconnection failed reason %s",
				tstats->targetHostname->str,
				torControl_getORConnReasonString(reason));
	}

	if (status == TORCTL_ORCONN_STATUS_CLOSED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s orconnection closed reason %s",
				tstats->targetHostname->str,
				torControl_getORConnReasonString(reason));
	}
}

static void _torcontrolstatistics_handleCircEvent(TorControlStatistics* tstats,
		gint code, gint circID, GString* path, gint status, gint buildFlags,
		gint purpose, gint reason, GDateTime* createTime) {
	/* log the params for debugging */
	gchar* timestr = g_date_time_format(createTime, "%Y-%m-%d_%H:%M:%S");
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i CIRC %i: cid=%i status=%i buildflags=%i purpose=%i reason=%i createtime=%s",
			tstats->targetHostname->str, tstats->targetPort, code, circID,
			status, buildFlags, purpose, reason, timestr);
	g_free(timestr);

	/* circuit build timeout */
	if (status == TORCTL_CIRC_STATUS_FAILED) {
		if (reason == TORCTL_CIRC_REASON_TIMEOUT) {
			GDateTime* failed = g_date_time_new_now_utc();
			GTimeSpan buildTimeoutMicros = g_date_time_difference(createTime,
					failed);

			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"host %s circuit %i failed reason %s %ld milliseconds",
					tstats->targetHostname->str, circID,
					torControl_getCircReasonString(reason),
					buildTimeoutMicros / 1000L);

			g_date_time_unref(failed);
		} else {
			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"host %s circuit %i failed reason %s",
					tstats->targetHostname->str, circID,
					torControl_getCircReasonString(reason));
		}
	}

	/* circuit was built */
	if (status == TORCTL_CIRC_STATUS_BUILT) {
		gboolean isInternal = FALSE, isOneHop = FALSE;
		if (buildFlags & TORCTL_CIRC_BUILD_FLAGS_IS_INTERNAL) {
			isInternal = TRUE;
		}
		if (buildFlags & TORCTL_CIRC_BUILD_FLAGS_ONEHOP_TUNNEL) {
			isOneHop = TRUE;
		}

		CircuitStats* cs = _circuitstats_new(createTime, circID, path, purpose,
				isInternal, isOneHop);
		cs->openTime = g_date_time_new_now_utc();
		g_hash_table_replace(tstats->circuits, &(cs->circuitID), cs);
	}

	if (status == TORCTL_CIRC_STATUS_CLOSED) {
		CircuitStats* cs = g_hash_table_lookup(tstats->circuits, &circID);
		if (cs) {
			cs->closeTime = g_date_time_new_now_utc();
			GTimeSpan buildTimeMicros = g_date_time_difference(cs->openTime,
					cs->launchTime);
			GTimeSpan runTimeMicros = g_date_time_difference(cs->closeTime,
					cs->openTime);
			g_assert(buildTimeMicros > 0 && runTimeMicros > 0);

			// TODO: log all of this circuits stats, remove from hashtable, free

//			g_printf("buildtime=%lu runtime=%lu\n", buildTimeMicros/1000L, runTimeMicros/1000L);

			g_hash_table_remove(tstats->circuits, &circID);
		}
	}
}

static void _torcontrolstatistics_handleStreamEvent(
		TorControlStatistics* tstats, gint code, gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
		gint remoteReason, gchar *source, in_addr_t sourceIP,
		in_port_t sourcePort, gint purpose) {
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i STREAM %i: sid=%i cid=%i targetIP=%u targetPort=%u status=%i "
					"reason=%i remoteReason=%i source=%s sourceIP=%u sourcePort=%u purpose=%i",
			tstats->targetHostname->str, tstats->targetPort, code, streamID,
			circID, targetIP, targetPort, status, reason, remoteReason, source,
			sourceIP, sourcePort, purpose);

	if (status == TORCTL_STREAM_STATUS_FAILED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s stream %i circuit %i failed reason %s",
				tstats->targetHostname->str, streamID, circID,
				torControl_getStreamReasonString(reason));
	}

	if (status == TORCTL_STREAM_STATUS_SUCCEEDED) {
		StreamStats* ss = _streamstats_new(streamID, circID, targetIP,
				targetPort, purpose);
		ss->openTime = g_date_time_new_now_utc();
		g_hash_table_replace(tstats->streams, &(ss->streamID), ss);
	}

	if (status == TORCTL_STREAM_STATUS_CLOSED) {
		StreamStats* ss = g_hash_table_lookup(tstats->streams, &streamID);
		if (ss) {
			ss->closeTime = g_date_time_new_now_utc();
			GTimeSpan runTimeMicros = g_date_time_difference(ss->closeTime,
					ss->openTime);
			g_assert(runTimeMicros > 0);

			// TODO: log all of this streams stats, remove from hashtable, free

//			g_printf("runtime=%lu\n", runTimeMicros/1000L);

			g_hash_table_remove(tstats->streams, &streamID);
		}
	}

}

static void _torcontrolstatistics_handleBWEvent(TorControlStatistics* tstats,
		gint code, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
		"%s:%u BW %i: read=%i write=%i", tstats->targetHostname->str,
		tstats->targetPort, code, bytesRead, bytesWritten);
}

static void _torcontrolstatistics_handleExtendedBWEvent(TorControlStatistics* tstats,
		gchar* type, gint code, gint id, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
		"%s:%u BW %i: type=%s id=%i read=%i write=%i", tstats->targetHostname->str,
		tstats->targetPort, code, type, id, bytesRead, bytesWritten);

    if (!g_ascii_strcasecmp(type, "STREAM_BW")) {

    } else if (!g_ascii_strcasecmp(type, "ORCONN_BW")) {

    } else if (!g_ascii_strcasecmp(type, "DIRCONN_BW")) {

    } else if (!g_ascii_strcasecmp(type, "EXITCONN_BW")) {

    }
}

static void _torcontrolstatistics_handleCellStatsEvent(TorControlStatistics* tstats,
		gint code, gint circID, gint nextHopCircID,
		gint appProcessed, gint appTotalWaitMillis, double appMeanQueueLength,
		gint exitProcessed, gint exitTotalWaitMillis, double exitMeanQueueLength) {
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
		"%s:%u CELL_STATS %i: circid=%i nextcircid=%i appproc=%i appwait=%i applen=%f "
		"exitproc=%i exitwait=%i exitlen=%f",
		tstats->targetHostname->str, tstats->targetPort, code,
		circID, nextHopCircID, appProcessed, appTotalWaitMillis, appMeanQueueLength,
		exitProcessed, exitTotalWaitMillis, exitMeanQueueLength);
}

/*
 * module setup and teardown
 */

static void _torcontrolstatistics_free(TorControlStatistics* tstats) {
	g_assert(tstats);

	g_string_free(tstats->targetHostname, TRUE);
	g_hash_table_destroy(tstats->circuits);
	g_hash_table_destroy(tstats->streams);

	g_free(tstats);
}

TorControlStatistics* torcontrolstatistics_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd, gchar **args,
		TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = (TorControlInitialize) _torcontrolstatistics_manageState;
	handlers->free = (TorControlFree) _torcontrolstatistics_free;
	handlers->circEvent = (TorControlCircEventFunc) _torcontrolstatistics_handleCircEvent;
	handlers->streamEvent = (TorControlStreamEventFunc) _torcontrolstatistics_handleStreamEvent;
	handlers->orconnEvent = (TorControlORConnEventFunc) _torcontrolstatistics_handleORConnEvent;
	handlers->bwEvent = (TorControlBWEventFunc) _torcontrolstatistics_handleBWEvent;
	handlers->extendedBWEvent = (TorControlExtendedBWEventFunc) _torcontrolstatistics_handleExtendedBWEvent;
	handlers->cellStatsEvent = (TorControlCellStatsEventFunc) _torcontrolstatistics_handleCellStatsEvent;
	handlers->responseEvent = (TorControlResponseFunc) _torcontrolstatistics_handleResponseEvent;

	TorControlStatistics* tstats = g_new0(TorControlStatistics, 1);

	tstats->log = logFunc;

	tstats->targetHostname = g_string_new(hostname);
	tstats->targetIP = ip;
	tstats->targetPort = port;
	tstats->targetSockd = sockd;

	tstats->currentState = TCS_SEND_AUTHENTICATE;

	tstats->circuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
			(GDestroyNotify) _circuitstats_free);
	tstats->streams = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
			(GDestroyNotify) _streamstats_free);

	return tstats;
}
