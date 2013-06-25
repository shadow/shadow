/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

RunnableFunctionTable killengine_functions = {
	(RunnableRunFunc) killengine_run,
	(RunnableFreeFunc) killengine_free,
	MAGIC_VALUE
};

KillEngineAction* killengine_new(guint64 endTimeInSeconds) {
	KillEngineAction* action = g_new0(KillEngineAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &killengine_functions);
	action->endTime = (SimulationTime)(SIMTIME_ONE_SECOND * endTimeInSeconds);

	return action;
}

void killengine_run(KillEngineAction* action) {
	MAGIC_ASSERT(action);
	worker_setKillTime(action->endTime);
}

void killengine_free(KillEngineAction* action) {
	MAGIC_ASSERT(action);
	MAGIC_CLEAR(action);
	g_free(action);
}
