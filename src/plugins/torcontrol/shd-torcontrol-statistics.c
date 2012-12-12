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

	GHashTable* connections;
	GHashTable* circuits;
	GHashTable* streams;
};

typedef struct _ConnectionStats ConnectionStats;
struct _ConnectionStats {
	gint connID;
	GString* target;

	GDateTime* openTime;
	GDateTime* closeTime;

	GList* bwReport;
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

	GList* bwReport;
	GList* cellReport;
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

	GList* bwReport;
};

typedef struct _BWReportItem BWReportItem;
struct _BWReportItem {
	GDateTime* stamp;
	guint read;
	guint write;
};

typedef struct _CellReportItem CellReportItem;
struct _CellReportItem {
	GDateTime* stamp;
	gint appProcessed;
	gint appTotalWaitMillis;
	double appMeanQueueLength;
	gint exitProcessed;
	gint exitTotalWaitMillis;
	double exitMeanQueueLength;
};


static BWReportItem* _bwreportitem_new(gint bytesRead, gint bytesWritten) {
	BWReportItem* item = g_new0(BWReportItem, 1);
	item->read = bytesRead;
	item->write = bytesWritten;
	item->stamp = g_date_time_new_now_utc();
	return item;
}

static void _bwreportitem_free(BWReportItem* item) {
	g_assert(item);
	if (item->stamp) {
		g_date_time_unref(item->stamp);
	}
	g_free(item);
}

static CellReportItem* _cellreportitem_new(gint appProcessed, gint appTotalWaitMillis, double appMeanQueueLength,
		gint exitProcessed, gint exitTotalWaitMillis, double exitMeanQueueLength) {
	CellReportItem* item = g_new0(CellReportItem, 1);

	item->appProcessed = appProcessed;
	item->appTotalWaitMillis = appTotalWaitMillis;
	item->appMeanQueueLength = appMeanQueueLength;
	item->exitProcessed = exitProcessed;
	item->exitTotalWaitMillis = exitTotalWaitMillis;
	item->exitMeanQueueLength = exitMeanQueueLength;
	item->stamp = g_date_time_new_now_utc();

	return item;
}

static void _cellreportitem_free(CellReportItem* item) {
	g_assert(item);
	if (item->stamp) {
		g_date_time_unref(item->stamp);
	}
	g_free(item);
}

static ConnectionStats* _connectionstats_new(gint connID, gchar* target) {
	ConnectionStats* cs = g_new0(ConnectionStats, 1);

	cs->connID = connID;
	cs->target = g_string_new(target);

	return cs;
}

static void _connectionstats_free(ConnectionStats* cs) {
	g_assert(cs);

	g_string_free(cs->target, TRUE);

	if (cs->openTime) {
		g_date_time_unref(cs->openTime);
	}
	if (cs->closeTime) {
		g_date_time_unref(cs->closeTime);
	}
	GList* iter = cs->bwReport;
	while(iter) {
		_bwreportitem_free((BWReportItem*)iter->data);
		iter = g_list_next(iter);
	}
	g_list_free(cs->bwReport);

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

	GList* iter = cs->bwReport;
	while(iter) {
		_bwreportitem_free((BWReportItem*)iter->data);
		iter = g_list_next(iter);
	}
	g_list_free(cs->bwReport);

	iter = cs->cellReport;
	while(iter) {
		_cellreportitem_free((CellReportItem*)iter->data);
		iter = g_list_next(iter);
	}
	g_list_free(cs->cellReport);

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

	GList* iter = ss->bwReport;
	while(iter) {
		_bwreportitem_free((BWReportItem*)iter->data);
		iter = g_list_next(iter);
	}
	g_list_free(ss->bwReport);

	g_free(ss);
}

static gchar* _torcontrolstatistics_bwReportToString(GList* bwReport) {
	GString* s = g_string_new("");

	GList* iter = bwReport;
	while(iter && iter->data) {
		BWReportItem* i = (BWReportItem*) iter->data;
		GString* temp = g_string_new(NULL);

		gint64 seconds = g_date_time_to_unix(i->stamp);
		g_string_append_printf(temp, "%li,%u,%u;", seconds, i->read, i->write);
		g_string_prepend(s, temp->str);

		g_string_free(temp, TRUE);
		iter = g_list_next(iter);
	}

	return g_string_free(s, FALSE);
}

static gchar* _torcontrolstatistics_cellReportToString(GList* cellReport) {
	GString* s = g_string_new("");

	GList* iter = cellReport;
	while(iter && iter->data) {
		CellReportItem* i = (CellReportItem*) iter->data;
		GString* temp = g_string_new(NULL);

		gint64 seconds = g_date_time_to_unix(i->stamp);
		g_string_append_printf(temp, "%li,%i,%i,%f,%i,%i,%f;", seconds,
				i->appProcessed, i->appTotalWaitMillis, i->appMeanQueueLength,
				i->exitProcessed, i->exitTotalWaitMillis, i->exitMeanQueueLength);
		g_string_prepend(s, temp->str);

		g_string_free(temp, TRUE);
		iter = g_list_next(iter);
	}

	return g_string_free(s, FALSE);
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
#ifdef DEBUG
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i ORCONN %i: id=%u target=%s status=%i reason=%i numcircs=%i",
			tstats->targetHostname->str, tstats->targetPort, code, connID, target,
			status, reason, numCircuits);
#endif

	if (status == TORCTL_ORCONN_STATUS_CONNECTED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s orconnection connected target %s",
				tstats->targetHostname->str, target);

		ConnectionStats* cs = _connectionstats_new(connID, target);
		cs->openTime = g_date_time_new_now_utc();
		g_hash_table_replace(tstats->connections, &(cs->connID), cs);
	}

	if (status == TORCTL_ORCONN_STATUS_FAILED) {
		tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host %s orconnection failed reason %s",
				tstats->targetHostname->str,
				torControl_getORConnReasonString(reason));
	}

	if (status == TORCTL_ORCONN_STATUS_CLOSED) {
		ConnectionStats* cs = g_hash_table_lookup(tstats->connections, &connID);
		if (cs) {
			cs->closeTime = g_date_time_new_now_utc();

			gint64 open = g_date_time_to_unix(cs->openTime);
			gint64 close = g_date_time_to_unix(cs->closeTime);
			gchar* bwreport = _torcontrolstatistics_bwReportToString(cs->bwReport);

			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host=%s:%i connection=%i open=%li close=%li bw=%s",
				tstats->targetHostname->str, tstats->targetPort, cs->connID,
				open, close, bwreport);

			g_free(bwreport);

			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"host %s orconnection closed reason %s",
					tstats->targetHostname->str,
					torControl_getORConnReasonString(reason));
		}
	}
}

static void _torcontrolstatistics_handleCircEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint circID, GString* path, gint status, gint buildFlags,
		gint purpose, gint reason, GDateTime* createTime) {
#ifdef DEBUG
	/* log the params for debugging */
	gchar* timestr = g_date_time_format(createTime, "%Y-%m-%d_%H:%M:%S");
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i CIRC %i: cid=%i status=%i buildflags=%i purpose=%i reason=%i createtime=%s path=%s",
			tstats->targetHostname->str, tstats->targetPort, code, circID,
			status, buildFlags, purpose, reason, timestr, (path ? path->str : ""));
	g_free(timestr);
#endif

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
			g_assert(buildTimeMicros > 0);

			gint64 open = g_date_time_to_unix(cs->openTime);
			gint64 close = g_date_time_to_unix(cs->closeTime);
			gchar* bwreport = _torcontrolstatistics_bwReportToString(cs->bwReport);
			gchar* cellreport = _torcontrolstatistics_cellReportToString(cs->cellReport);

			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host=%s:%i circuit=%i build=%li open=%li close=%li bw=%s cell=%s",
				tstats->targetHostname->str, tstats->targetPort, cs->circuitID,
				buildTimeMicros/1000L, open, close, bwreport, cellreport);

			g_free(bwreport);
			g_free(cellreport);

			/* remove (this also frees it) */
			g_hash_table_remove(tstats->circuits, &circID);
		}
	}
}

static void _torcontrolstatistics_handleStreamEvent(
		TorControlStatistics* tstats, gint code, gchar* line, gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
		gint remoteReason, gchar *source, in_addr_t sourceIP,
		in_port_t sourcePort, gint purpose) {
#ifdef DEBUG
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
			"%s:%i STREAM %i: sid=%i cid=%i targetIP=%u targetPort=%u status=%i "
					"reason=%i remoteReason=%i source=%s sourceIP=%u sourcePort=%u purpose=%i",
			tstats->targetHostname->str, tstats->targetPort, code, streamID,
			circID, targetIP, targetPort, status, reason, remoteReason, source,
			sourceIP, sourcePort, purpose);
#endif

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

			gint64 open = g_date_time_to_unix(ss->openTime);
			gint64 close = g_date_time_to_unix(ss->closeTime);
			gchar* bwreport = _torcontrolstatistics_bwReportToString(ss->bwReport);

			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"host=%s:%i stream=%i open=%li close=%li bw=%s",
				tstats->targetHostname->str, tstats->targetPort, ss->streamID,
				open, close, bwreport);

			g_free(bwreport);

			/* remove (this also frees it) */
			g_hash_table_remove(tstats->streams, &streamID);
		}
	}

}

static void _torcontrolstatistics_handleBWEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
		"%s:%u BW %i: read=%i write=%i", tstats->targetHostname->str,
		tstats->targetPort, code, bytesRead, bytesWritten);
}

static void _torcontrolstatistics_handleExtendedBWEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gchar* type, gint id, gint bytesRead, gint bytesWritten) {
#ifdef DEBUG
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
		"%s:%u BW %i: type=%s id=%i read=%i write=%i", tstats->targetHostname->str,
		tstats->targetPort, code, type, id, bytesRead, bytesWritten);
#endif

    if (!g_ascii_strcasecmp(type, "STREAM_BW")) {
		StreamStats* ss = g_hash_table_lookup(tstats->streams, &id);
		if (ss) {
			BWReportItem* streamItem = _bwreportitem_new(bytesRead, bytesWritten);
			ss->bwReport = g_list_prepend(ss->bwReport, streamItem);

			CircuitStats* cs = g_hash_table_lookup(tstats->circuits, &(ss->circuitID));
			if (cs) {
				gboolean doCreateNew = TRUE;

				/* should we append to an existing circuit report, or add one */
				if(cs->bwReport && cs->bwReport->data) {
					BWReportItem* lastItem = cs->bwReport->data;
					GTimeSpan micros = g_date_time_difference(lastItem->stamp, streamItem->stamp);
					if(micros < 1000000) { /* one second */
						doCreateNew = FALSE;
						lastItem->read += bytesRead;
						lastItem->write += bytesWritten;
					}
				}

				if(doCreateNew) {
					BWReportItem* circItem = _bwreportitem_new(bytesRead, bytesWritten);
					cs->bwReport = g_list_prepend(cs->bwReport, circItem);
				}
			}
		}
    } else if (!g_ascii_strcasecmp(type, "ORCONN_BW")) {
		ConnectionStats* cs = g_hash_table_lookup(tstats->connections, &id);
		if (cs) {
			BWReportItem* i = _bwreportitem_new(bytesRead, bytesWritten);
			cs->bwReport = g_list_prepend(cs->bwReport, i);
		}
    } else if (!g_ascii_strcasecmp(type, "DIRCONN_BW") || !g_ascii_strcasecmp(type, "EXITCONN_BW")) {
    	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%u BW %i: type=%s id=%i read=%i write=%i", tstats->targetHostname->str,
			tstats->targetPort, code, type, id, bytesRead, bytesWritten);
    }
}

static void _torcontrolstatistics_handleCellStatsEvent(TorControlStatistics* tstats,
		gint code, gchar* line, gint circID, gint nextHopCircID, gint prevHopCircID,
		gint appProcessed, gint appTotalWaitMillis, double appMeanQueueLength,
		gint exitProcessed, gint exitTotalWaitMillis, double exitMeanQueueLength) {
#ifdef DEBUG
	tstats->log(G_LOG_LEVEL_DEBUG, __FUNCTION__,
		"%s:%u CELL_STATS %i: circid=%i nextcircid=%i prevcircid=%i appproc=%i appwait=%i applen=%f "
		"exitproc=%i exitwait=%i exitlen=%f",
		tstats->targetHostname->str, tstats->targetPort, code,
		circID, nextHopCircID, prevHopCircID,
		appProcessed, appTotalWaitMillis, appMeanQueueLength,
		exitProcessed, exitTotalWaitMillis, exitMeanQueueLength);
#endif

	CircuitStats* cs = g_hash_table_lookup(tstats->circuits, &circID);
	if (cs) {
		CellReportItem* i = _cellreportitem_new(appProcessed, appTotalWaitMillis, appMeanQueueLength,
				exitProcessed, exitTotalWaitMillis, exitMeanQueueLength);
		cs->cellReport = g_list_prepend(cs->cellReport, i);
	}
}

/*
 * module setup and teardown
 */

static void _torcontrolstatistics_free(TorControlStatistics* tstats) {
	g_assert(tstats);

	g_string_free(tstats->targetHostname, TRUE);
	g_hash_table_destroy(tstats->connections);
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

	tstats->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
			(GDestroyNotify) _connectionstats_free);
	tstats->circuits = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
			(GDestroyNotify) _circuitstats_free);
	tstats->streams = g_hash_table_new_full(g_int_hash, g_int_equal, NULL,
			(GDestroyNotify) _streamstats_free);

	return tstats;
}
