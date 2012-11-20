/*
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

#ifndef SHD_TOR_CTL_CIRCUITBUILD_H_
#define SHD_TOR_CTL_CIRCUITBUILD_H_

#include <glib.h>
#include <shd-library.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <event.h>

#include "shd-torcontrol.h"

enum TorCtlCircuitBuild_State {
	TORCTL_CIRCBUILD_STATE_AUTHENTICATE,
	TORCTL_CIRCBUILD_STATE_SETCONFS,
	TORCTL_CIRCBUILD_STATE_SETEVENTS,
	TORCTL_CIRCBUILD_STATE_CHECKSTATUS,
	TORCTL_CIRCBUILD_STATE_CREATE_CIRCUIT,
	TORCTL_CIRCBUILD_STATE_GET_CIRC_ID,
	TORCTL_CIRCBUILD_STATE_ATTACH_STREAMS,
};

typedef struct _TorCtlCircuitBuild TorCtlCircuitBuild;
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

TorCtlCircuitBuild *torControlCircuitBuild_new(ShadowLogFunc logFunc, gint sockd, gchar **args, TorControl_EventHandlers *handlers);

gint torControlCircuitBuild_initialize(gpointer moduleData);
void torControlCircuitBuild_free(TorCtlCircuitBuild* circuitBuild);

void torControlCircuitBuild_circEvent(gpointer moduleData, gint code, gint circID, gint status,
		gint buildFlags, gint purpose, gint reason);
void torControlCircuitBuild_streamEvent(gpointer moduleData, gint code, gint streamID, gint circID,
		in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
		gint remoteReason, gchar *source, in_addr_t sourceIP, in_port_t sourcePort,
		gint purpose);
void torControlCircuitBuild_orConnEvent(gpointer moduleData, gint code, gchar *target, gint status,
		gint reason, gint numCircuits);
void torControlCircuitBuild_bwEvent(gpointer moduleData, gint code, gint bytesRead, gint bytesWritten);
void torControlCircuitBuild_logEvent(gpointer moduleData, gint code, gint severity, gchar *msg);
void torControlCircuitBuild_responseEvent(gpointer moduleData, GList *reply, gpointer userData);

#endif /* SHD_TOR_CTL_CIRCUITBUILD_H_ */
