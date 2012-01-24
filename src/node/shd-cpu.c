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
	guint frequencyMHz;
	guint rawFrequencyMHz;
	gdouble frequencyRatio;
	SimulationTime threshold;
	SimulationTime now;
	SimulationTime timeCPUAvailable;
	MAGIC_DECLARE;
};

CPU* cpu_new(guint frequencyMHz, gint threshold) {
	CPU* cpu = g_new0(CPU, 1);
	MAGIC_INIT(cpu);

	cpu->frequencyMHz = frequencyMHz;
	cpu->threshold = threshold > 0 ? (threshold * SIMTIME_ONE_MICROSECOND) : SIMTIME_INVALID;
	cpu->timeCPUAvailable = cpu->now = 0;

	/* get the raw speed of the experiment machine */
	gchar* contents = NULL;
	gsize length = 0;
	GError* error = NULL;

	/* get the original file */
	if(!g_file_get_contents(CONFIG_CPU_MAX_FREQ_FILE, &contents, &length, &error)) {
		critical("unable to read '%s' for copying: %s", CONFIG_CPU_MAX_FREQ_FILE, error->message);
		cpu->rawFrequencyMHz = cpu->frequencyMHz;
		cpu->frequencyRatio = 1.0;
	} else {
		cpu->rawFrequencyMHz = (guint)atoi(contents);
		cpu->frequencyRatio = (gdouble)((gdouble)cpu->rawFrequencyMHz) / ((gdouble)cpu->frequencyMHz);
	}

	return cpu;
}

void cpu_free(CPU* cpu) {
	MAGIC_ASSERT(cpu);
	MAGIC_CLEAR(cpu);
	g_free(cpu);
}

SimulationTime cpu_getDelay(CPU* cpu) {
	MAGIC_ASSERT(cpu);

	/* we only have delay if we've crossed the threshold */
	SimulationTime builtUpDelay = cpu->timeCPUAvailable - cpu->now;
	if(builtUpDelay > cpu->threshold) {
		return builtUpDelay;
	}
	return 0;
}

gboolean cpu_isBlocked(CPU* cpu) {
	MAGIC_ASSERT(cpu);
	return cpu_getDelay(cpu) > 0;
}

void cpu_updateTime(CPU* cpu, SimulationTime now) {
	MAGIC_ASSERT(cpu);
	cpu->now = now;
	/* the time available is now if we have no delay, otherwise no change
	 * this is important so that our delay is added from now or into the future
	 */
	cpu->timeCPUAvailable = (SimulationTime) MAX(cpu->timeCPUAvailable, now);
}

void cpu_addDelay(CPU* cpu, SimulationTime delay) {
	MAGIC_ASSERT(cpu);
	SimulationTime adjustedDelay = (SimulationTime) (cpu->frequencyRatio * delay);
	cpu->timeCPUAvailable += adjustedDelay;
}
