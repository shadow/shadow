/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

typedef struct _Master Master;

Master* master_new(Configuration*);
void master_free(Master*);
void master_run(Master*);

void master_updateMinTimeJump(Master*, gdouble);
GTimer* master_getRunTimer(Master*);
SimulationTime master_getEndTime(Master*);
void master_setEndTime(Master*, SimulationTime);

gboolean master_slaveFinishedCurrentRound(Master*, SimulationTime, SimulationTime*, SimulationTime*);

#endif /* SHD_ENGINE_H_ */
