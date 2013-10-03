/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
	ShadowCreateCallbackFunc createCallback;

	enum torcontrolpinger_state currentState;
	enum torcontrolpinger_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;

	gchar* pingRelay;
	GHashTable* outstandingPings;
};

/* 'ping' a single tor relay and record the RTT
 * the ping is really an circ extend to one relay hop, then a circ destroy. */
static void _torcontrolpinger_doPingCallback(TorControlPinger* tcp) {
	g_assert(tcp);
	/* send an EXTENDCIRCUIT command, the result and the circID
	 * will pop up in _torcontrolpinger_handleResponseEvent
	 */
	torControl_buildCircuit(tcp->targetSockd, tcp->pingRelay);
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
			tcp->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "set tor control events 'CIRC'");
		}
		break;
	}

	case TCPS_RECV_SETEVENTS: {
		/* all done */
		tcp->currentState = TCPS_IDLE;
		tcp->nextState = TCPS_IDLE;
		_torcontrolpinger_doPingCallback(tcp);
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
		tcp->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "[%d] ERROR: %s",
				replyLine->code, replyLine->body);
		break;
	}

	case TORCTL_REPLY_SUCCESS: {
		tcp->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "[%d] SUCCESS: %s",
				replyLine->code, replyLine->body);

		/* check if this is a successful start of a ping */
		gchar** parts = g_strsplit(replyLine->body, " ", 10);
		if(!g_strcmp0(parts[0], "EXTENDED")) {
			/* take circID and start time for ping RTT */
			GDateTime* pingStartTime = g_date_time_new_now_utc();
			gint* circID = g_new0(gint, 1);
			*circID = (gint) g_ascii_strtoll(parts[1], NULL, 10);
			g_hash_table_replace(tcp->outstandingPings, circID, pingStartTime);

			tcp->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "ping started for circ %i", *circID);
		}

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

	tcp->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "[torcontrol-ping] %s:%i %s",
			tcp->targetHostname->str, tcp->targetPort, line);

	/* check if this is a ping circuit */
	GDateTime* pingStartTime = g_hash_table_lookup(tcp->outstandingPings, &circID);
	if(pingStartTime) {
		tcp->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "got ping start for circ %i", circID);

		/* if it was successfully extended, record the time */
		if(status == TORCTL_CIRC_STATUS_EXTENDED) {
			GDateTime* pingEndTime = g_date_time_new_now_utc();

			GTimeSpan pingMicros = g_date_time_difference(pingEndTime, pingStartTime);
			GTimeSpan pingMillis = (GTimeSpan) (pingMicros / 1000);
			GTimeSpan pingMicrosCirc = g_date_time_difference(pingEndTime, createTime);
			GTimeSpan pingMillisCirc = (GTimeSpan) (pingMicrosCirc / 1000);

			tcp->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"[torcontrol-ping] %s:%i pinger pinged %s "
				"on circ %i in %ld millis (%ld millis since create)",
				tcp->targetHostname->str, tcp->targetPort, path->str,
				circID, pingMillis, pingMillisCirc);

			/* we no longer need the ping circuit */
			g_date_time_unref(pingEndTime);
		}

		if(status == TORCTL_CIRC_STATUS_EXTENDED ||
				status == TORCTL_CIRC_STATUS_FAILED ||
				status == TORCTL_CIRC_STATUS_CLOSED) {
			/* remove no mater what - it may have extended or failed or closed */
			tcp->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "ping circ %i %s", circID,
					torControl_getCircStatusString(status));

			g_hash_table_remove(tcp->outstandingPings, &circID);
			torControl_closeCircuit(tcp->targetSockd, circID);

			/* start another ping to this relay in 1 second */
			tcp->createCallback((ShadowPluginCallbackFunc)_torcontrolpinger_doPingCallback, tcp, 1000);
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
	g_hash_table_destroy(tcp->outstandingPings);

	g_free(tcp);
}

TorControlPinger* torcontrolpinger_new(ShadowLogFunc logFunc, ShadowCreateCallbackFunc cbFunc,
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
		logFunc(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "Error! Did not specify pingRelay to ping!");
		return NULL;
	}

	TorControlPinger* tcp = g_new0(TorControlPinger, 1);

	tcp->log = logFunc;
	tcp->createCallback = cbFunc;

	tcp->targetHostname = g_string_new(hostname);
	tcp->targetIP = ip;
	tcp->targetPort = port;
	tcp->targetSockd = sockd;

	/* store the pingRelay name so we can "ping" it by extending a circuit to it,
	 * and then destroying the circuit */
	tcp->pingRelay = g_strdup(moduleArgs[0]);
	g_assert(tcp->pingRelay);

	/* holds outstanding pings as a GDateTime by a gint circID */
	tcp->outstandingPings = g_hash_table_new_full(g_int_hash, g_int_equal,
			g_free, (GDestroyNotify)g_date_time_unref);

	tcp->currentState = TCPS_SEND_AUTHENTICATE;

	return tcp;
}
