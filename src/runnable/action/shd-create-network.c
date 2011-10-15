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

RunnableFunctionTable createnetwork_functions = {
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

	action_init(&(action->super), &createnetwork_functions);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->latencyID = g_quark_from_string(latencyCDFName->str);
	action->reliability = reliability;

	return action;
}

void createnetwork_run(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

	CumulativeDistribution* cdf = engine_get(worker->cached_engine, CDFS, action->latencyID);
	if(!cdf) {
		critical("failed to create network '%s'", g_quark_to_string(action->latencyID));
		return;
	}

	internetwork_createNetwork(worker->cached_engine->internet, action->id, cdf, action->reliability);
}

void createnetwork_free(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
