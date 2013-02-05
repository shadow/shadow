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
#include "shd-torcontrol-pinger.h"

enum torcontrolpinger_state {
	TCPS_IDLE,
	TCPS_SEND_AUTHENTICATE, TCPS_RECV_AUTHENTICATE,
	TCPS_SEND_SETEVENTS, TCPS_RECV_SETEVENTS,
};

struct _TorControlPinger {
	ShadowLogFunc log;
	enum torcontrolpinger_state currentState;
	enum torcontrolpinger_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;

	GDateTime* pingStartTime;
	gchar* pingRelay;
};

static void _torcontrolpinger_doPingAsExtend(TorControlPinger* tcp) {
	/* start a ping if there is not one in progress */
	if(!tcp->pingStartTime) {
		tcp->pingStartTime = g_date_time_new_now_utc();
		torControl_buildCircuit(tcp->targetSockd, tcp->pingRelay);
	}
}

/*
 * setting up and registering with the ControlPort
 */

static gboolean _torcontrolpinger_manageState(TorControlPinger* tcp) {

	beginmanage: switch (tcp->currentState) {

	case TCPS_SEND_AUTHENTICATE: {
		/* authenticate with the control port */
		if (torControl_authenticate(tcp->targetSockd, "password") > 0) {
			/* idle until we receive the response, then move to next state */
			tcp->currentState = TCPS_IDLE;
			tcp->nextState = TCPS_RECV_AUTHENTICATE;
		}
		break;
	}

	case TCPS_RECV_AUTHENTICATE: {
		tcp->currentState = TCPS_SEND_SETEVENTS;
		goto beginmanage;
		break;
	}

	case TCPS_SEND_SETEVENTS: {
		/* send list of events to listen on */
		if (torControl_setevents(tcp->targetSockd, "CIRC") > 0) {
			/* idle until we receive the response, then move to next state */
			tcp->currentState = TCPS_IDLE;
			tcp->nextState = TCPS_RECV_SETEVENTS;
			tcp->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "set tor control events 'CIRC'");
		}
		break;
	}

	case TCPS_RECV_SETEVENTS: {
		/* all done */
		tcp->currentState = TCPS_IDLE;
		tcp->nextState = TCPS_IDLE;
		_torcontrolpinger_doPingAsExtend(tcp);
		goto beginmanage;
		break;
	}

	case TCPS_IDLE: {
		if (tcp->nextState == TCPS_IDLE) {
			return TRUE;
		}
		break;
	}

	default:
		break;
	}

	return FALSE;
}

static void _torcontrolpinger_handleResponseEvent(TorControlPinger* tcp,
		GList *reply, gpointer userData) {
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

	switch (TORCTL_CODE_TYPE(replyLine->code)) {
	case TORCTL_REPLY_ERROR: {
		tcp->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s",
				replyLine->code, replyLine->body);
		break;
	}

	case TORCTL_REPLY_SUCCESS: {
		tcp->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s",
				replyLine->code, replyLine->body);
		tcp->currentState = tcp->nextState;
		_torcontrolpinger_manageState(tcp);
		break;
	}

	default:
		break;
	}
}

/*
 * handling the asynchronous events from control port
 */

static void _torcontrolpinger_handleCircEvent(TorControlPinger* tcp, gint code,
		gchar* line, gint circID, GString* path, gint status, gint buildFlags,
		gint purpose, gint reason, GDateTime* createTime) {
	tcp->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "[torcontrol-ping] %s:%i %s",
			tcp->targetHostname->str, tcp->targetPort, line);

	if(status == TORCTL_CIRC_STATUS_EXTENDED && tcp->pingStartTime &&
			path && !g_strcmp0(tcp->pingRelay, path->str)) {
		GDateTime* pingEndTime = g_date_time_new_now_utc();

		GTimeSpan pingMicros = g_date_time_difference(pingEndTime, tcp->pingStartTime);
		GTimeSpan pingMicrosCirc = g_date_time_difference(pingEndTime, createTime);

		/* make sure we are checking the correct ping circuit and not
		 * some other tor circuit thats being built in the background */
		if(pingMicros == pingMicrosCirc) {
			GTimeSpan pingMillis = (GTimeSpan) pingMicros / 1000;

			tcp->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"[torcontrol-ping] %s:%i pinger pinged %s in %ld millis %ld",
					tcp->targetHostname->str, tcp->targetPort, path->str, pingMillis);

			g_date_time_unref(pingEndTime);
			g_date_time_unref(tcp->pingStartTime);
			tcp->pingStartTime = NULL;

			torControl_closeCircuit(tcp->targetSockd, circID);
			_torcontrolpinger_doPingAsExtend(tcp);
		}
	}
}

/*
 * module setup and teardown
 */

static void _torcontrolpinger_free(TorControlPinger* tcp) {
	g_assert(tcp);

	g_string_free(tcp->targetHostname, TRUE);
	g_free(tcp->pingRelay);

	g_free(tcp);
}

TorControlPinger* torcontrolpinger_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd,
		gchar **moduleArgs, TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = (TorControlInitialize) _torcontrolpinger_manageState;
	handlers->free = (TorControlFree) _torcontrolpinger_free;
	handlers->responseEvent = (TorControlResponseFunc) _torcontrolpinger_handleResponseEvent;

	/* all events get processed by the same function that simply logs tor output directly */
	handlers->circEvent = (TorControlCircEventFunc) _torcontrolpinger_handleCircEvent;

	/* make sure they specified events */
	if(!moduleArgs[0]) {
		logFunc(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error! Did not specify pingRelay to ping!");
		return NULL;
	}

	TorControlPinger* tcp = g_new0(TorControlPinger, 1);

	tcp->log = logFunc;

	tcp->targetHostname = g_string_new(hostname);
	tcp->targetIP = ip;
	tcp->targetPort = port;
	tcp->targetSockd = sockd;

	/* store the pingRelay name so we can "ping" it by extending a circuit to it,
	 * and then destroying the circuit */
	tcp->pingRelay = g_strdup(moduleArgs[0]);
	g_assert(tcp->pingRelay);

	tcp->currentState = TCPS_SEND_AUTHENTICATE;

	return tcp;
}
