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
