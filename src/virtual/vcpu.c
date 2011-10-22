/*
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

#include <glib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "shadow.h"

vcpu_tp vcpu_create(guint64 cpu_speed_Bps) {
	vcpu_tp vcpu = malloc(sizeof(vcpu_t));

	vcpu->cpu_speed_Bps = cpu_speed_Bps;
	vcpu->nanos_per_cpu_aes_byte = 1000000000.0 / cpu_speed_Bps;
	vcpu->nanos_per_cpu_proc_byte = vcpu->nanos_per_cpu_aes_byte * VCPU_AES_TO_TOR_RATIO;
	vcpu->timeCPUAvailable = vcpu->now = 0;

	return vcpu;
}

void vcpu_destroy(vcpu_tp vcpu) {
	if(vcpu != NULL) {
		free(vcpu);
	}
}

static void vcpu_add_load(vcpu_tp vcpu, gdouble load) {
	/* convert bytes into nanoseconds to give our node a notion of cpu delay.
	 * to incorporate general runtime, we multiply by VCPU_LOAD_MULTIPLIER here. */
	if(vcpu != NULL) {
		guint64 ns_to_add = (guint64) ceil(load);
		vcpu->timeCPUAvailable += ns_to_add;
		debug("added %lu nanos of CPU load. CPU is ready at %lu", ns_to_add, vcpu->timeCPUAvailable);
	}
}

void vcpu_add_load_aes(vcpu_tp vcpu, guint32 bytes) {
	if(vcpu != NULL) {
		gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
		gdouble load = adjusted_bytes * vcpu->nanos_per_cpu_aes_byte;
		vcpu_add_load(vcpu, load);
	}
}

void vcpu_add_load_read(vcpu_tp vcpu, guint32 bytes) {
	if(vcpu != NULL) {
		gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
		gdouble load = adjusted_bytes * vcpu->nanos_per_cpu_proc_byte * VCPU_READ_FRACTION;
		vcpu_add_load(vcpu, load);
	}
}

void vcpu_add_load_write(vcpu_tp vcpu, guint32 bytes) {
	if(vcpu != NULL) {
		gdouble adjusted_bytes = (gdouble)(VCPU_LOAD_MULTIPLIER * bytes);
		gdouble load = adjusted_bytes * vcpu->nanos_per_cpu_proc_byte * VCPU_WRITE_FRACTION;
		vcpu_add_load(vcpu, load);
	}
}

SimulationTime _vcpu_getDelay(vcpu_tp vcpu) {
	/* we only have delay if we've crossed the threshold */
	SimulationTime builtUpDelay = vcpu->timeCPUAvailable - vcpu->now;
	if(builtUpDelay > VCPU_DELAY_THRESHOLD_NS) {
		return builtUpDelay;
	}
	return 0;
}

gboolean vcpu_isBlocked(vcpu_tp vcpu) {
	g_assert(vcpu);
	return _vcpu_getDelay(vcpu) > 0;
}

SimulationTime vcpu_adjustDelay(vcpu_tp vcpu, SimulationTime now) {
	g_assert(vcpu);
	vcpu->now = now;
	vcpu->timeCPUAvailable = (SimulationTime) fmax(vcpu->timeCPUAvailable, now);
	return _vcpu_getDelay(vcpu);
}
