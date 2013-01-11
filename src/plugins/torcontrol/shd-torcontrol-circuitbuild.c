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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <assert.h>
#include <event.h>

#include <string.h>

#include "shd-torcontrol-circuitbuild.h"

enum TorCtlCircuitBuild_State {
	TORCTL_CIRCBUILD_STATE_AUTHENTICATE,
	TORCTL_CIRCBUILD_STATE_SETCONFS,
	TORCTL_CIRCBUILD_STATE_SETEVENTS,
	TORCTL_CIRCBUILD_STATE_CHECKSTATUS,
	TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT,
	TORCTL_CIRCBUILD_STATE_GET_CIRC_ID,
	TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS,
};

struct _TorCtlCircuitBuild {
	ShadowLogFunc log;

	gchar bootstrapped;
	GList *circuit;
	gint sockd;
	gint circID;
	GList *streamsToAttach;
	gchar waitingForResponse;
	enum TorCtlCircuitBuild_State state;
	enum TorCtlCircuitBuild_State nextState;
};

static gboolean _torControlCircuitBuild_initialize(gpointer moduleData) {
    TorCtlCircuitBuild *circuitBuild = moduleData;
    ShadowLogFunc log = circuitBuild->log;

    gint sockd = circuitBuild->sockd;
    gboolean initialized = 0;

    if(circuitBuild->waitingForResponse) {
        return 0;
    }

    switch(circuitBuild->state) {
        case TORCTL_CIRCBUILD_STATE_AUTHENTICATE: {
            /* authenticate with the control */
            if(torControl_authenticate(sockd, "password") > 0) {
                circuitBuild->nextState = TORCTL_CIRCBUILD_STATE_SETCONFS;
                circuitBuild->waitingForResponse = TRUE;
            }
            break;
        }

        case TORCTL_CIRCBUILD_STATE_SETCONFS: {
            /* set configuration variables */
            gchar *confValues[7] = {"__LeaveStreamsUnattached", "1", "ExcludeSingleHopRelays", "0", "AllowSingleHopCircuits", "1", NULL};
            if(torControl_setconf(sockd, confValues) > 0) {
                circuitBuild->nextState = TORCTL_CIRCBUILD_STATE_SETEVENTS;
                circuitBuild->waitingForResponse = TRUE;
            }
            break;
        }

        case TORCTL_CIRCBUILD_STATE_SETEVENTS: {
            /* send list of events to listen on */
            if(torControl_setevents(sockd, "CIRC STREAM STATUS_CLIENT") > 0) {
                circuitBuild->nextState = TORCTL_CIRCBUILD_STATE_CHECKSTATUS;
                circuitBuild->waitingForResponse = TRUE;
            }
            break;
        }

        case TORCTL_CIRCBUILD_STATE_CHECKSTATUS: {
            /* check the bootstrap status of the node */
            if(torControl_getInfoBootstrapStatus(sockd) > 0) {
                circuitBuild->nextState = TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT;
                circuitBuild->waitingForResponse = TRUE;
                initialized = TRUE;
            }
            break;
        }

        case TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT:
        case TORCTL_CIRCBUILD_STATE_GET_CIRC_ID:
        case TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS:
        	break;
    }

    return initialized;
}

static void _torControlCircuitBuild_circEvent(gpointer moduleData, gint code, gchar* line, gint circID, GString* path, gint status,
		gint buildFlags, gint purpose, gint reason, GDateTime* createTime) {
	TorCtlCircuitBuild *circuitBuild = moduleData;
	ShadowLogFunc log = circuitBuild->log;

	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "[%d] CIRC: circID=%d status=%d buildFlags=%d purpose=%d reason=%d",
			code, circID, status, buildFlags, purpose, reason);

	/* if our circuit was closed, build new one */
	if(circID == circuitBuild->circID && status == TORCTL_CIRC_STATUS_CLOSED) {
	    torControl_buildCircuit(circuitBuild->sockd, circuitBuild->circuit);
	    circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
	}
}

static void _torControlCircuitBuild_streamEvent(gpointer moduleData, gint code, gchar* line, gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
		gint remoteReason, gchar *source, in_addr_t sourceIP, in_port_t sourcePort,
		gint purpose) {
	TorCtlCircuitBuild *circuitBuild = moduleData;
	ShadowLogFunc log = circuitBuild->log;

	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "[%d] STREAM: status=\"%s\" streamID=%d  circID=%d",
	        code, torControl_getStreamStatusString(status), streamID, circID);

	if(status == TORCTL_STREAM_STATUS_NEW && circuitBuild->bootstrapped) {
	    /* if we're in the process of building a circuit, add stream ID to list so we can reattach it later */
        if(circuitBuild->state == TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS) {
            torControl_attachStream(circuitBuild->sockd, streamID, circuitBuild->circID);
        } else {
            circuitBuild->streamsToAttach = g_list_append(circuitBuild->streamsToAttach, GINT_TO_POINTER(streamID));
        }
	}
}

static void _torControlCircuitBuild_statusEvent(gpointer moduleData, gint code, gchar* line,
        gint type, gint severity, gchar *action, GHashTable *arguments) {
    TorCtlCircuitBuild *circuitBuild = moduleData;
    ShadowLogFunc log = circuitBuild->log;

    if(type == TORCTL_STATUS_TYPE_CLIENT && !g_ascii_strcasecmp(action, "BOOTSTRAP")) {
        gchar *progress = g_hash_table_lookup(arguments, "PROGRESS");
        if(!progress) {
            log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Could not find argument PROGRESS in bootstrap status");
        } else if(!g_ascii_strcasecmp(progress, "100")) {
            circuitBuild->bootstrapped = TRUE;
            torControl_buildCircuit(circuitBuild->sockd, circuitBuild->circuit);
            circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
        }
    }
}

static void _torControlCircuitBuild_responseEvent(gpointer moduleData, GList *reply, gpointer userData) {
	TorCtlCircuitBuild *circuitBuild = moduleData;
	ShadowLogFunc log = circuitBuild->log;

	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;
	switch(TORCTL_CODE_TYPE(replyLine->code)) {
        case TORCTL_REPLY_ERROR: {
            log(G_LOG_LEVEL_WARNING, __FUNCTION__, "[%d] ERROR: %s", replyLine->code, replyLine->body);
            break;
        }

	    case TORCTL_REPLY_SUCCESS: {
	        log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%d] SUCCESS: %s", replyLine->code, replyLine->body);

	        switch(circuitBuild->state) {
	            case TORCTL_CIRCBUILD_STATE_AUTHENTICATE:
	            case TORCTL_CIRCBUILD_STATE_SETCONFS:
	            case TORCTL_CIRCBUILD_STATE_SETEVENTS:
	                circuitBuild->state = circuitBuild->nextState;
	                circuitBuild->waitingForResponse = FALSE;
	                break;

	            case TORCTL_CIRCBUILD_STATE_CHECKSTATUS: {
	                if(userData) {
                        TorControl_BootstrapPhase *phase = userData;
                        if(phase->progress == 100) {
                            circuitBuild->bootstrapped = TRUE;
                            torControl_buildCircuit(circuitBuild->sockd, circuitBuild->circuit);
                            circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
                        } else {
                            circuitBuild->state = TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT;
                        }
	                }
                    break;
	            }

	            case TORCTL_CIRCBUILD_STATE_GET_CIRC_ID: {
	                if(userData) {
                        TorControl_ReplyExtended *extended = userData;
                        circuitBuild->circID = extended->circID;
                        circuitBuild->state = TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS;

                        /* go through and reattach streams to this circuit */
                        for(GList *iter = circuitBuild->streamsToAttach; iter; iter = g_list_next(iter)) {
                            gint streamID = (gint)iter->data;
                            torControl_attachStream(circuitBuild->sockd, streamID, circuitBuild->circID);
                        }

                        g_list_free(circuitBuild->streamsToAttach);
                        circuitBuild->streamsToAttach = NULL;
	                }
                    break;
	            }

	            case TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT:
	            case TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS:
	            	break;
            }
	    }

	    /* make sure we proceed with the next state */
	    _torControlCircuitBuild_initialize(moduleData);

	    break;
	}
}

static void _torControlCircuitBuild_free(TorCtlCircuitBuild* circuitBuild) {
	// FIXME
}

TorCtlCircuitBuild *torControlCircuitBuild_new(ShadowLogFunc logFunc, gint sockd, gchar **args, TorControl_EventHandlers *handlers) {
    g_assert(handlers && args);

	handlers->initialize = _torControlCircuitBuild_initialize;
    handlers->free = (TorControlFree) _torControlCircuitBuild_free;
	handlers->circEvent = _torControlCircuitBuild_circEvent;
	handlers->streamEvent = _torControlCircuitBuild_streamEvent;
	handlers->statusEvent = _torControlCircuitBuild_statusEvent;
	handlers->responseEvent = _torControlCircuitBuild_responseEvent;


	if(!args[0]) {
		logFunc(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error! Did not specify circuit to build!");
		return NULL;
	}

	/* create structure with module data to return */
	TorCtlCircuitBuild *circuitBuild = g_new0(TorCtlCircuitBuild, 1);
	circuitBuild->log = logFunc;
	circuitBuild->sockd = sockd;

	circuitBuild->circuit = NULL;
	gchar **nodes = g_strsplit(args[0], ",", 0);
	for(gint idx = 0; nodes[idx]; idx++) {
		circuitBuild->circuit = g_list_append(circuitBuild->circuit, strdup(nodes[idx]));
	}
	g_strfreev(nodes);

	circuitBuild->state = TORCTL_CIRCBUILD_STATE_AUTHENTICATE;

	logFunc(G_LOG_LEVEL_INFO, __FUNCTION__, "Successfully initialized the circuit build Tor control module.");

	return circuitBuild;
}
