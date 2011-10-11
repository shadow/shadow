/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"

RunnableVTable connectnetwork_vtable = { (RunnableRunFunc) connectnetwork_run,
		(RunnableFreeFunc) connectnetwork_free, MAGIC_VALUE };

ConnectNetworkAction* connectnetwork_new(GString* networkaName,
		GString* networkbName, GString* latencyabCDFName,
		gdouble reliabilityab, GString* latencybaCDFName,
		gdouble reliabilityba)
{
	g_assert(networkaName && networkbName && latencyabCDFName && latencybaCDFName);
	ConnectNetworkAction* action = g_new0(ConnectNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &connectnetwork_vtable);

	action->networkaID = g_quark_from_string((const gchar*) networkaName->str);
	action->networkbID = g_quark_from_string((const gchar*) networkbName->str);
	action->latencyabCDFID = g_quark_from_string((const gchar*) latencyabCDFName->str);
	action->latencybaCDFID = g_quark_from_string((const gchar*) latencybaCDFName->str);
	action->reliabilityab = reliabilityab;
	action->reliabilityba = reliabilityba;

	return action;
}

void connectnetwork_run(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	CumulativeDistribution* cdfA2B = registry_get(worker->cached_engine->registry, CDFS, &(action->latencyabCDFID));
	CumulativeDistribution* cdfB2A = registry_get(worker->cached_engine->registry, CDFS, &(action->latencybaCDFID));

	if(cdfA2B && cdfB2A) {
		topology_add_edge(worker->cached_engine->topology, action->networkaID, cdfA2B, action->reliabilityab, action->networkbID, cdfB2A, action->reliabilityba);
	} else {
		critical("failed to add edge to topology");
	}
}

void connectnetwork_free(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
