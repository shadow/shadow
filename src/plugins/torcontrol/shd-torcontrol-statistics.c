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

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;

	gboolean waitingForResponse;
	enum torcontrolstatistic_state currentState;
	enum torcontrolstatistic_state nextState;
};

static gboolean _torcontrolstatistics_manageState(TorControlStatistics* tstats) {

beginmanage:
	switch(tstats->currentState) {

		case TCS_SEND_AUTHENTICATE: {
            /* authenticate with the control port */
            if(torControl_authenticate(tstats->targetSockd, "password") > 0) {
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
            if(torControl_setevents(tstats->targetSockd, "CIRC STREAM ORCONN BW") > 0) {
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
			if(tstats->nextState == TCS_IDLE) {
				return TRUE;
			}
			break;
		}

		default:
			break;
	}

	return FALSE;
}

static gboolean _torcontrolstatistics_initialize(TorControlStatistics* tstats) {
	return _torcontrolstatistics_manageState(tstats);
}

static void _torcontrolstatistics_handleORConnEvent(TorControlStatistics* tstats,
		gint code, gchar *target, gint status, gint reason, gint numCircuits) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i ORCONN %i: target=%s status=%i reason=%i numcircs=%i",
			tstats->targetHostname->str, tstats->targetPort, code,
			target, status, reason, numCircuits);
}

static void _torcontrolstatistics_handleCircEvent(TorControlStatistics* tstats,
		gint code, gint circID, gint status, gint buildFlags, gint purpose,
		gint reason) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i CIRC %i: cid=%i status=%i buildflags=%i purpose=%i reason=%i",
			tstats->targetHostname->str, tstats->targetPort, code,
			circID, status, buildFlags, purpose, reason);
}

static void _torcontrolstatistics_handleStreamEvent(TorControlStatistics* tstats,
		gint code, gint streamID, gint circID, in_addr_t targetIP,
		in_port_t targetPort, gint status, gint reason, gint remoteReason,
		gchar *source, in_addr_t sourceIP, in_port_t sourcePort, gint purpose) {
//	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
//			"%s:%i STREAM %i: sid=%i cid=%i destip",
//			tstats->targetHostname->str, tstats->targetPort, code);
}

static void _torcontrolstatistics_handleBWEvent(TorControlStatistics* tstats,
		gint code, gint bytesRead, gint bytesWritten) {
	tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
			"%s:%i BW %i: read=%i write=%i",
			tstats->targetHostname->str, tstats->targetPort, code, bytesRead,
			bytesWritten);
}

static void _torcontrolstatistics_handleResponseEvent(TorControlStatistics* tstats,
		GList *reply, gpointer userData) {
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

	switch(TORCTL_CODE_TYPE(replyLine->code)) {
		case TORCTL_REPLY_ERROR: {
			tstats->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s", replyLine->code, replyLine->body);
			break;
		}

		case TORCTL_REPLY_SUCCESS: {
			tstats->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s", replyLine->code, replyLine->body);
			tstats->currentState = tstats->nextState;
			_torcontrolstatistics_manageState(tstats);
			break;
		}

		default:
			break;
	}
}

static void _torcontrolstatistics_free(TorControlStatistics* tstats) {
	g_assert(tstats);

	g_string_free(tstats->targetHostname, TRUE);

	g_free(tstats);
}

TorControlStatistics* torcontrolstatistics_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd, gchar **args,
		TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = _torcontrolstatistics_initialize;
    handlers->free = _torcontrolstatistics_free;
	handlers->circEvent = _torcontrolstatistics_handleCircEvent;
	handlers->streamEvent = _torcontrolstatistics_handleStreamEvent;
	handlers->orconnEvent = _torcontrolstatistics_handleORConnEvent;
	handlers->bwEvent = _torcontrolstatistics_handleBWEvent;
	handlers->responseEvent = _torcontrolstatistics_handleResponseEvent;

	TorControlStatistics* tstats = g_new0(TorControlStatistics, 1);

	tstats->log = logFunc;

	tstats->targetHostname = g_string_new(hostname);
	tstats->targetIP = ip;
	tstats->targetPort = port;
	tstats->targetSockd = sockd;

	tstats->currentState = TCS_SEND_AUTHENTICATE;

	return tstats;
}
