/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CPU_H_
#define SHD_CPU_H_

#include <glib.h>

#include "main/core/support/definitions.h"

typedef struct _CPU CPU;

CPU* cpu_new(guint64 frequencyKHz, guint64 rawFrequencyKHz, guint64 threshold, guint64 precision);
void cpu_free(CPU* cpu);

gboolean cpu_isBlocked(CPU* cpu);
void cpu_updateTime(CPU* cpu, SimulationTime now);
void cpu_addDelay(CPU* cpu, SimulationTime delay);
SimulationTime cpu_getDelay(CPU* cpu);

#endif /* SHD_CPU_H_ */
