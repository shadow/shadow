/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
