/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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
