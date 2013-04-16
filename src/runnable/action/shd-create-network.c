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
