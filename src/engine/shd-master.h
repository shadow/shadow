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
gint master_run(Master* master);

SimulationTime master_getMinTimeJump(Master* master);
void master_updateMinTimeJump(Master* master, gdouble minPathLatency);
SimulationTime master_getExecutionBarrier(Master* master);
GTimer* master_getRunTimer(Master* master);
void master_setKillTime(Master* master, SimulationTime endTime);
gboolean master_isKilled(Master* master);
void master_setKilled(Master* master, gboolean isKilled);
SimulationTime master_getExecuteWindowEnd(Master* master);
SimulationTime master_getExecuteWindowStart(Master* master);
SimulationTime master_getEndTime(Master* master);
void master_slaveFinishedCurrentWindow(Master* master, SimulationTime minNextEventTime);

#endif /* SHD_ENGINE_H_ */
