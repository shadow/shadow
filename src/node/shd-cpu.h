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

/* this is multiplied by the actual number of bytes processed to artificially increase processing penalty.
 * set to 0 to disable CPU load delays. */
#define VCPU_LOAD_MULTIPLIER 1

/* how long until we block reads and writes? 1 milliseconds */
#define VCPU_DELAY_THRESHOLD_NS 1000000

/* ratio of AES speed to Tor application processing speed as in a PlanetLab experiment */
#define VCPU_AES_TO_TOR_RATIO 24.0
/* estimate of the fraction of time taken to read vs write */
#define VCPU_READ_FRACTION 0.75
#define VCPU_WRITE_FRACTION 1 - VCPU_READ_FRACTION

typedef struct _CPU CPU;

CPU* cpu_new(guint64 cpu_speed_Bps);
void cpu_free(CPU* cpu);

void cpu_add_load_aes(CPU* cpu, guint32 bytes);
void cpu_add_load_read(CPU* cpu, guint32 bytes);
void cpu_add_load_write(CPU* cpu, guint32 bytes);
gboolean cpu_isBlocked(CPU* cpu);

SimulationTime cpu_adjustDelay(CPU* cpu, SimulationTime now);

#endif /* SHD_CPU_H_ */
