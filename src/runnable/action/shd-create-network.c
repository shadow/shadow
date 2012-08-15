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

#include "shadow.h"

struct _CreateNetworkAction {
	Action super;
	GQuark id;
	guint64 bandwidthdown;
	guint64 bandwidthup;
	gdouble packetloss;
	MAGIC_DECLARE;
};

RunnableFunctionTable createnetwork_functions = {
	(RunnableRunFunc) createnetwork_run,
	(RunnableFreeFunc) createnetwork_free,
	MAGIC_VALUE
};

CreateNetworkAction* createnetwork_new(GString* name, guint64 bandwidthdown,
		guint64 bandwidthup, gdouble packetloss) {
	g_assert(name);
	CreateNetworkAction* action = g_new0(CreateNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnetwork_functions);

	action->id = g_quark_from_string((const gchar*)name->str);
	action->bandwidthdown = bandwidthdown;
	action->bandwidthup = bandwidthup;
	action->packetloss = packetloss;

	return action;
}

void createnetwork_run(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	internetwork_createNetwork(worker_getInternet(), action->id,
			action->bandwidthdown, action->bandwidthup, action->packetloss);
}

void createnetwork_free(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
