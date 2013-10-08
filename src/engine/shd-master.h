/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

typedef struct _Master Master;

Master* master_new(Configuration* config);
void master_free(Master* engine);
void master_run(Master* master);

SimulationTime master_getMinTimeJump(Master* master);
SimulationTime master_getExecutionBarrier(Master* master);
GTimer* master_getRunTimer(Master* master);
void master_setKillTime(Master* master, SimulationTime endTime);
gboolean master_isKilled(Master* master);
void master_setKilled(Master* master, gboolean isKilled);
SimulationTime master_getExecuteWindowEnd(Master* master);
void master_setExecuteWindowEnd(Master* master, SimulationTime end);
SimulationTime master_getExecuteWindowStart(Master* master);
void master_setExecuteWindowStart(Master* master, SimulationTime start);
SimulationTime master_getEndTime(Master* master);

#endif /* SHD_ENGINE_H_ */
