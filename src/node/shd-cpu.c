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

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "shadow.h"

struct _CPU {
	guint64 cpu_speed_Bps;
	gdouble nanos_per_cpu_aes_byte;
	gdouble nanos_per_cpu_proc_byte;
	SimulationTime now;
	SimulationTime timeCPUAvailable;
	MAGIC_DECLARE;
};

CPU* cpu_new(guint64 cpu_speed_Bps) {
	CPU* cpu = g_new0(CPU, 1);
	MAGIC_INIT(cpu);

	cpu->cpu_speed_Bps = cpu_speed_Bps;
	cpu->nanos_per_cpu_aes_byte = 1000000000.0 / cpu_speed_Bps;
	cpu->nanos_per_cpu_proc_byte = cpu->nanos_per_cpu_aes_byte * VCPU_AES_TO_TOR_RATIO;
	cpu->timeCPUAvailable = cpu->now = 0;

	return cpu;
}

void cpu_free(CPU* cpu) {
	MAGIC_ASSERT(cpu);
	MAGIC_CLEAR(cpu);
	g_free(cpu);
}

static void cpu_add_load(CPU* cpu, gdouble load) {
	MAGIC_ASSERT(cpu);

	/* convert bytes into nanoseconds to give our node a notion of cpu delay.
	 * to incorporate general runtime, we multiply by VCPU_LOAD_MULTIPLIER here. */
	guint64 ns_to_add = (guint64) ceil(load);
	cpu->timeCPUAvailable += ns_to_add;
	debug("added %lu nanos of CPU load. CPU is ready at %lu", ns_to_add, cpu->timeCPUAvailable);
}

void cpu_add_load_aes(CPU* cpu, guint32 bytes) {
	MAGIC_ASSERT(cpu);

	gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
	gdouble load = adjusted_bytes * cpu->nanos_per_cpu_aes_byte;
	cpu_add_load(cpu, load);
}

void cpu_add_load_read(CPU* cpu, guint32 bytes) {
	MAGIC_ASSERT(cpu);

	gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
	gdouble load = adjusted_bytes * cpu->nanos_per_cpu_proc_byte * VCPU_READ_FRACTION;
	cpu_add_load(cpu, load);
}

void cpu_add_load_write(CPU* cpu, guint32 bytes) {
	MAGIC_ASSERT(cpu);

	gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
	gdouble load = adjusted_bytes * cpu->nanos_per_cpu_proc_byte * VCPU_WRITE_FRACTION;
	cpu_add_load(cpu, load);
}

static SimulationTime _cpu_getDelay(CPU* cpu) {
	return 0;
//	TODO fix CPU delay modelling
//	MAGIC_ASSERT(cpu);
//
//	/* we only have delay if we've crossed the threshold */
//	SimulationTime builtUpDelay = cpu->timeCPUAvailable - cpu->now;
//	if(builtUpDelay > VCPU_DELAY_THRESHOLD_NS) {
//		return builtUpDelay;
//	}
//	return 0;
}

gboolean cpu_isBlocked(CPU* cpu) {
	MAGIC_ASSERT(cpu);
	return _cpu_getDelay(cpu) > 0;
}

SimulationTime cpu_adjustDelay(CPU* cpu, SimulationTime now) {
	MAGIC_ASSERT(cpu);
	cpu->now = now;
	cpu->timeCPUAvailable = (SimulationTime) MAX(cpu->timeCPUAvailable, now);
	return _cpu_getDelay(cpu);
}
