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

RunnableVTable createnetwork_vtable = {
	(RunnableRunFunc) createnetwork_run,
	(RunnableFreeFunc) createnetwork_free,
	MAGIC_VALUE
};

CreateNetworkAction* createnetwork_new(GString* name, GString* latencyCDFName,
		gdouble reliability)
{
	g_assert(name && latencyCDFName);
	CreateNetworkAction* action = g_new0(CreateNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnetwork_vtable);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->latencyCDFName = g_string_new(latencyCDFName->str);
	action->reliability = reliability;

	return action;
}

void createnetwork_run(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	GQuark cdfID = g_quark_from_string((const gchar*)action->latencyCDFName->str);
	CumulativeDistribution* cdf = registry_get(worker->cached_engine->registry, CDFS, &cdfID);
	if(cdf) {
		topology_add_vertex(worker->cached_engine->topology, action->id, cdf, action->reliability);
	} else {
		critical("failed to add vertex to topology");
	}

	Network* network = network_new(action->id);
	registry_put(worker->cached_engine->registry, NETWORKS, &(network->id), network);
}

void createnetwork_free(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->latencyCDFName, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
