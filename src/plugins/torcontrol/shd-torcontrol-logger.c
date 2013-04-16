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

#include <glib.h>

#include "shd-torcontrol.h"
#include "shd-torcontrol-logger.h"

enum torcontrollogger_state {
	TCLS_IDLE,
	TCLS_SEND_AUTHENTICATE, TCLS_RECV_AUTHENTICATE,
	TCLS_SEND_SETEVENTS, TCLS_RECV_SETEVENTS,
};

struct _TorControlLogger {
	ShadowLogFunc log;
	enum torcontrollogger_state currentState;
	enum torcontrollogger_state nextState;

	GString* targetHostname;
	in_addr_t targetIP;
	in_port_t targetPort;
	gint targetSockd;
	gchar* torctlEvents;
};

/*
 * setting up and registering with the ControlPort
 */

static gboolean _torcontrollogger_manageState(TorControlLogger* tcl) {

	beginmanage: switch (tcl->currentState) {

	case TCLS_SEND_AUTHENTICATE: {
		/* authenticate with the control port */
		if (torControl_authenticate(tcl->targetSockd, "password") > 0) {
			/* idle until we receive the response, then move to next state */
			tcl->currentState = TCLS_IDLE;
			tcl->nextState = TCLS_RECV_AUTHENTICATE;
		}
		break;
	}

	case TCLS_RECV_AUTHENTICATE: {
		tcl->currentState = TCLS_SEND_SETEVENTS;
		goto beginmanage;
		break;
	}

	case TCLS_SEND_SETEVENTS: {
		/* send list of events to listen on */
		if (torControl_setevents(tcl->targetSockd, tcl->torctlEvents) > 0) {
			/* idle until we receive the response, then move to next state */
			tcl->currentState = TCLS_IDLE;
			tcl->nextState = TCLS_RECV_SETEVENTS;
			tcl->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "set tor control events '%s'", tcl->torctlEvents);
		}
		break;
	}

	case TCLS_RECV_SETEVENTS: {
		/* all done */
		tcl->currentState = TCLS_IDLE;
		tcl->nextState = TCLS_IDLE;
		goto beginmanage;
		break;
	}

	case TCLS_IDLE: {
		if (tcl->nextState == TCLS_IDLE) {
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
	g_free(tcl->torctlEvents);

	g_free(tcl);
}

TorControlLogger* torcontrollogger_new(ShadowLogFunc logFunc,
		gchar* hostname, in_addr_t ip, in_port_t port, gint sockd,
		gchar **moduleArgs, TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = (TorControlInitialize) _torcontrollogger_manageState;
	handlers->free = (TorControlFree) _torcontrollogger_free;
	handlers->responseEvent = (TorControlResponseFunc) _torcontrollogger_handleResponseEvent;

	/* all events get processed by the same function that simply logs tor output directly */
	handlers->genericEvent = (TorControlGenericEventFunc) _torcontrollogger_handleEvents;

	/* make sure they specified events */
	if(!moduleArgs[0]) {
		logFunc(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error! Did not specify torctl events to log!");
		return NULL;
	}

	TorControlLogger* tcl = g_new0(TorControlLogger, 1);

	tcl->log = logFunc;

	tcl->targetHostname = g_string_new(hostname);
	tcl->targetIP = ip;
	tcl->targetPort = port;
	tcl->targetSockd = sockd;

	/* store the events a string so we can register it later
	 * g_ascii_strup duplicates and converts the str to uppercase
	 * g_strdelimit replaces ',' with ' ' in place */
	tcl->torctlEvents = g_strdelimit(g_ascii_strup(moduleArgs[0], -1), ",", ' ');
	g_assert(tcl->torctlEvents);

	tcl->currentState = TCLS_SEND_AUTHENTICATE;

	return tcl;
}
