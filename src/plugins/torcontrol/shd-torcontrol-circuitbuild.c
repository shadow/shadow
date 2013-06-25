/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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

typedef struct _TorCtlCircuitBuild_Circuit TorCtlCircuitBuild_Circuit;
struct _TorCtlCircuitBuild_Circuit {
	gint startTime, endTime;
	gchar *relays;
	gint circID;
};

struct _TorCtlCircuitBuild {
	ShadowLogFunc log;

	gchar bootstrapped;
	GList *circuits;
	GQueue *circuitsToBuild;
	gint sockd;
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
	if(status == TORCTL_CIRC_STATUS_CLOSED) {
		TorCtlCircuitBuild_Circuit *circuit = NULL;
		for(GList *iter = g_list_first(circuitBuild->circuits); iter && !circuit; iter = g_list_next(iter)) {
			TorCtlCircuitBuild_Circuit *tmp = iter->data;
			if(tmp->circID == circID) {
				circuit = tmp;
			}
		}

		if(circuit) {
			log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "circuit %d closed, rebuilding", circuit->circID);
			g_queue_push_tail(circuitBuild->circuitsToBuild, circuit);
			if(g_queue_get_length(circuitBuild->circuitsToBuild) == 1) {
				torControl_buildCircuit(circuitBuild->sockd, circuit->relays);
			}
			circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
		}
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
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);

		TorCtlCircuitBuild_Circuit *circuit = NULL;
		for(GList *iter = g_list_first(circuitBuild->circuits); iter && !circuit; iter = g_list_next(iter)) {
			TorCtlCircuitBuild_Circuit *tmp = iter->data;
			if(now.tv_sec >= tmp->startTime && (now.tv_sec < tmp->endTime || tmp->endTime == -1)) {
				circuit = tmp;
			}
		}

		if(circuit) {
			torControl_attachStream(circuitBuild->sockd, streamID, circuit->circID);
		} else {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Could not find any circuit time span for stream %d", streamID);
			torControl_attachStream(circuitBuild->sockd, streamID, 0);
		}

//	    /* if we're in the process of building a circuit, add stream ID to list so we can reattach it later */
//        if(circuitBuild->state == TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS) {
//            torControl_attachStream(circuitBuild->sockd, streamID, circuitBuild->circID);
//        } else {
//            circuitBuild->streamsToAttach = g_list_append(circuitBuild->streamsToAttach, GINT_TO_POINTER(streamID));
//        }
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
            TorCtlCircuitBuild_Circuit *circuit = g_queue_peek_head(circuitBuild->circuitsToBuild);
            if(circuit) {
            	torControl_buildCircuit(circuitBuild->sockd, circuit->relays);
            	circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
            } else {
            	log(G_LOG_LEVEL_WARNING, __FUNCTION__, "No circuit found to build");
            }
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
                            TorCtlCircuitBuild_Circuit *circuit = g_queue_peek_head(circuitBuild->circuitsToBuild);
							torControl_buildCircuit(circuitBuild->sockd, circuit->relays);
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
//                        circuitBuild->circID = extended->circID;
                        circuitBuild->state = TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS;

                        /* pop off circuit from queue, assign circ ID and check for any more to create */
                        TorCtlCircuitBuild_Circuit *circuit = g_queue_pop_head(circuitBuild->circuitsToBuild);
                        circuit->circID = extended->circID;
                        if(g_queue_get_length(circuitBuild->circuitsToBuild) > 0) {
                        	TorCtlCircuitBuild_Circuit *circuit = g_queue_peek_head(circuitBuild->circuitsToBuild);
							torControl_buildCircuit(circuitBuild->sockd, circuit->relays);
							circuitBuild->state = TORCTL_CIRCBUILD_STATE_GET_CIRC_ID;
                        }

//                        /* go through and reattach streams to this circuit */
//                        for(GList *iter = circuitBuild->streamsToAttach; iter; iter = g_list_next(iter)) {
//                            gint streamID = (gint)iter->data;
//                            torControl_attachStream(circuitBuild->sockd, streamID, circuitBuild->circID);
//                        }
//
//                        g_list_free(circuitBuild->streamsToAttach);
//                        circuitBuild->streamsToAttach = NULL;
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

TorCtlCircuitBuild *torControlCircuitBuild_new(ShadowLogFunc logFunc, gint sockd,
		gchar **moduleArgs, TorControl_EventHandlers *handlers) {
    g_assert(handlers && moduleArgs);

	handlers->initialize = _torControlCircuitBuild_initialize;
    handlers->free = (TorControlFree) _torControlCircuitBuild_free;
	handlers->circEvent = _torControlCircuitBuild_circEvent;
	handlers->streamEvent = _torControlCircuitBuild_streamEvent;
	handlers->statusEvent = _torControlCircuitBuild_statusEvent;
	handlers->responseEvent = _torControlCircuitBuild_responseEvent;


	if(!moduleArgs[0]) {
		logFunc(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error! Did not specify circuit to build!");
		return NULL;
	}

	/* create structure with module data to return */
	TorCtlCircuitBuild *circuitBuild = g_new0(TorCtlCircuitBuild, 1);
	circuitBuild->log = logFunc;
	circuitBuild->sockd = sockd;

	circuitBuild->circuits = NULL;
	circuitBuild->circuitsToBuild = g_queue_new();

	for(gint idx = 0; moduleArgs[idx]; idx++) {
		logFunc(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s", moduleArgs[idx]);
		TorCtlCircuitBuild_Circuit *circuit = g_new0(TorCtlCircuitBuild_Circuit, 1);
		circuit->startTime = 0;
		circuit->endTime = -1;
		circuit->relays = NULL;
		circuit->circID = -1;

		/* time:hop1,hop2,hop3 OR hop1,hop2,hop3 */
		gchar **parts = g_strsplit(moduleArgs[idx], ":", 2);
		if(!parts[1]) {
			circuit->relays = g_strdup(parts[0]);
		} else {
			circuit->startTime = g_ascii_strtoll(parts[0], NULL, 10);
			circuit->relays = g_strdup(parts[1]);
		}
		g_strfreev(parts);

		circuitBuild->circuits = g_list_append(circuitBuild->circuits, circuit);
		g_queue_push_tail(circuitBuild->circuitsToBuild, circuit);
	}


	for(GList *iter = g_list_first(circuitBuild->circuits); iter && iter->next; iter = g_list_next(iter)) {
		TorCtlCircuitBuild_Circuit *circ1 = iter->data;
		TorCtlCircuitBuild_Circuit *circ2 = iter->next->data;
		circ1->endTime = circ2->startTime;
	}

//	gchar **nodes = g_strsplit(moduleArgs[0], ",", 0);
//	for(gint idx = 0; nodes[idx]; idx++) {
//		circuitBuild->circuit = g_list_append(circuitBuild->circuit, strdup(nodes[idx]));
//	}
//	g_strfreev(nodes);

	circuitBuild->state = TORCTL_CIRCBUILD_STATE_AUTHENTICATE;

	logFunc(G_LOG_LEVEL_INFO, __FUNCTION__, "Successfully initialized the circuit build Tor control module.");

	return circuitBuild;
}
