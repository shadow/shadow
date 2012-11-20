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

};

static gboolean torcontrolstatistics_initialize(TorControlStatistics* torstats) {
	return FALSE;
}

static void torcontrolstatistics_circEvent(TorControlStatistics* tstats,
		gint code, gint circID, gint status, gint buildFlags, gint purpose,
		gint reason) {

}

static void torcontrolstatistics_streamEvent(TorControlStatistics* tstats,
		gint code, gint streamID, gint circID, in_addr_t targetIP,
		in_port_t targetPort, gint status, gint reason, gint remoteReason,
		gchar *source, in_addr_t sourceIP, in_port_t sourcePort, gint purpose) {

}

static void torcontrolstatistics_orConnEvent(TorControlStatistics* tstats,
		gint code, gchar *target, gint status, gint reason, gint numCircuits) {

}

static void torcontrolstatistics_bwEvent(TorControlStatistics* tstats,
		gint code, gint bytesRead, gint bytesWritten) {

}

static void torcontrolstatistics_responseEvent(TorControlStatistics* tstats,
		GList *reply, gpointer userData) {

}

static void torcontrolstatistics_free(TorControlStatistics* tstats) {
	g_assert(tstats);
	g_free(tstats);
}

TorControlStatistics* torcontrolstatistics_new(ShadowLogFunc logFunc, gint sockd,
		gchar **args, TorControl_EventHandlers *handlers) {
	g_assert(handlers);

	handlers->initialize = torcontrolstatistics_initialize;
    handlers->free = torcontrolstatistics_free;
	handlers->circEvent = torcontrolstatistics_circEvent;
	handlers->streamEvent = torcontrolstatistics_streamEvent;
	handlers->orconnEvent = torcontrolstatistics_orConnEvent;
	handlers->bwEvent = torcontrolstatistics_bwEvent;
	handlers->responseEvent = torcontrolstatistics_responseEvent;

	TorControlStatistics* tcstats = g_new0(TorControlStatistics, 1);

	return tcstats;
}
