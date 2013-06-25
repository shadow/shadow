/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CPU_H_
#define SHD_CPU_H_

typedef struct _CPU CPU;

CPU* cpu_new(guint frequencyMHz, gint threshold, gint precision);
void cpu_free(CPU* cpu);

gboolean cpu_isBlocked(CPU* cpu);
void cpu_updateTime(CPU* cpu, SimulationTime now);
void cpu_addDelay(CPU* cpu, SimulationTime delay);
SimulationTime cpu_getDelay(CPU* cpu);

#endif /* SHD_CPU_H_ */
