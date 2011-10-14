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
