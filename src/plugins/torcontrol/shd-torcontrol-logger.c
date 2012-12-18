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
#include "shd-torcontrol-logger.h"

struct _TorControlLogger {
	ShadowLogFunc log;
	enum torcontrollogger_state currentState;
	enum torcontrollogger_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;
};

/*
 * setting up and registering with the ControlPort
 */

static gboolean _torcontrollogger_manageState(TorControlLogger* tcl) {

	beginmanage: switch (tcl->currentState) {

	case TCS_SEND_AUTHENTICATE: {
		/* authenticate with the control port */
		if (torControl_authenticate(tcl->targetSockd, "password") > 0) {
			/* idle until we receive the response, then move to next state */
			tcl->currentState = TCS_IDLE;
			tcl->nextState = TCS_RECV_AUTHENTICATE;
		}
		break;
	}

	case TCS_RECV_AUTHENTICATE: {
		tcl->currentState = TCS_SEND_SETEVENTS;
		goto beginmanage;
		break;
	}

	case TCS_SEND_SETEVENTS: {
		/* send list of events to listen on */
		if (torControl_setevents(tcl->targetSockd,
				"CIRC STREAM ORCONN BW STREAM_BW") > 0) {
			/* idle until we receive the response, then move to next state */
			tcl->currentState = TCS_IDLE;
			tcl->nextState = TCS_RECV_SETEVENTS;
		}
		break;
	}

	case TCS_RECV_SETEVENTS: {
		/* all done */
		tcl->currentState = TCS_IDLE;
		tcl->nextState = TCS_IDLE;
		goto beginmanage;
		break;
	}

	case TCS_IDLE: {
		if (tcl->nextState == TCS_IDLE) {
			return TRUE;
		}
		break;
	}

	default:
		break;
	}

	return FALSE;
}

static void _torcontrollogger_handleResponseEvent(TorControlLogger* tcl,
		GList *reply, gpointer userData) {
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

	switch (TORCTL_CODE_TYPE(replyLine->code)) {
	case TORCTL_REPLY_ERROR: {
		tcl->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s",
				replyLine->code, replyLine->body);
		break;
	}

	case TORCTL_REPLY_SUCCESS: {
		tcl->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s",
				replyLine->code, replyLine->body);
		tcl->currentState = tcl->nextState;
		_torcontrollogger_manageState(tcl);
		break;
	}

	default:
		break;
	}
}

/*
 * handling the asynchronous events from control port
 */

static void _torcontrollogger_handleEvents(TorControlLogger* tcl, gint code,
		gchar* line, ...) {
	tcl->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[torcontrol-log] %s:%i %s",
			tcl->targetHostname->str, tcl->targetPort, line);
}

/*
 * module setup and teardown
 */

static void _torcontrollogger_free(TorControlLogger* tcl) {
	g_assert(tcl);

	g_string_free(tcl->targetHostname, TRUE);

	g_free(tcl);
}

TorControlLogger* torcontrollogger_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd, gchar **args,
		TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = (TorControlInitialize) _torcontrollogger_manageState;
	handlers->free = (TorControlFree) _torcontrollogger_free;
	handlers->responseEvent = (TorControlResponseFunc) _torcontrollogger_handleResponseEvent;

	handlers->circEvent = (TorControlCircEventFunc) _torcontrollogger_handleEvents;
	handlers->streamEvent = (TorControlStreamEventFunc) _torcontrollogger_handleEvents;
	handlers->orconnEvent = (TorControlORConnEventFunc) _torcontrollogger_handleEvents;
	handlers->bwEvent = (TorControlBWEventFunc) _torcontrollogger_handleEvents;
	handlers->extendedBWEvent = (TorControlExtendedBWEventFunc) _torcontrollogger_handleEvents;
	handlers->cellStatsEvent = (TorControlCellStatsEventFunc) _torcontrollogger_handleEvents;
	handlers->tokenEvent = (TorControlTokenEventFunc) _torcontrollogger_handleEvents;
	handlers->orTokenEvent = (TorControlORTokenEventFunc) _torcontrollogger_handleEvents;

	TorControlLogger* tcl = g_new0(TorControlLogger, 1);

	tcl->log = logFunc;

	tcl->targetHostname = g_string_new(hostname);
	tcl->targetIP = ip;
	tcl->targetPort = port;
	tcl->targetSockd = sockd;

	tcl->currentState = TCS_SEND_AUTHENTICATE;

	return tcl;
}
