/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_KILL_ENGINE_H_
#define SHD_KILL_ENGINE_H_

#include "shadow.h"

typedef struct _KillEngineAction KillEngineAction;

struct _KillEngineAction {
	Action super;
	SimulationTime endTime;
	MAGIC_DECLARE;
};

KillEngineAction* killengine_new(guint64 endTimeInSeconds);
void killengine_run(KillEngineAction* action);
void killengine_free(KillEngineAction* action);

#endif /* SHD_KILL_ENGINE_H_ */
