/**
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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#include "vcpu.h"
#include "vtransport_mgr.h"
#include "sim.h"
#include "global.h"
#include "log.h"

vcpu_tp vcpu_create(uint64_t cpu_speed_Bps) {
	vcpu_tp vcpu = malloc(sizeof(vcpu_t));

	vcpu->cpu_speed_Bps = cpu_speed_Bps;
	vcpu->nanos_per_cpu_aes_byte = 1000000000.0 / cpu_speed_Bps;
	vcpu->nanos_per_cpu_proc_byte = vcpu->nanos_per_cpu_aes_byte * VCPU_AES_TO_TOR_RATIO;
	vcpu->nanos_accumulated_delay = 0;
	vcpu->nanos_currently_absorbed = 0;

	return vcpu;
}

void vcpu_destroy(vcpu_tp vcpu) {
	if(vcpu != NULL) {
		free(vcpu);
	}
}

static void vcpu_add_load(vcpu_tp vcpu, double load) {
	/* convert bytes into nanoseconds to give our node a notion of cpu delay.
	 * to incorporate general runtime, we multiply by VCPU_LOAD_MULTIPLIER here. */
	if(vcpu != NULL) {
		uint64_t ns_to_add = (uint64_t) ceil(load);
		vcpu->nanos_accumulated_delay += ns_to_add;
		debugf("vcpu_add_load: added %lu nanos of CPU load. new load is %lu\n", ns_to_add, vcpu->nanos_accumulated_delay);
	}
}

void vcpu_add_load_aes(vcpu_tp vcpu, uint32_t bytes) {
	if(vcpu != NULL) {
		double adjusted_bytes = (double)(VCPU_LOAD_MULTIPLIER * bytes);
		double load = adjusted_bytes * vcpu->nanos_per_cpu_aes_byte;
		vcpu_add_load(vcpu, load);
	}
}

void vcpu_add_load_read(vcpu_tp vcpu, uint32_t bytes) {
	if(vcpu != NULL) {
		double adjusted_bytes = (double)(VCPU_LOAD_MULTIPLIER * bytes);
		double load = adjusted_bytes * vcpu->nanos_per_cpu_proc_byte * VCPU_READ_FRACTION;
		vcpu_add_load(vcpu, load);
	}
}

void vcpu_add_load_write(vcpu_tp vcpu, uint32_t bytes) {
	if(vcpu != NULL) {
		double adjusted_bytes = (double)(VCPU_LOAD_MULTIPLIER * bytes);
		double load = adjusted_bytes * vcpu->nanos_per_cpu_proc_byte * VCPU_WRITE_FRACTION;
		vcpu_add_load(vcpu, load);
	}
}

uint8_t vcpu_is_blocking(vcpu_tp vcpu) {
	if(vcpu != NULL) {
		/* we have delay if we've crossed the threshold */
		uint64_t unabsorbed_delay = vcpu->nanos_accumulated_delay - vcpu->nanos_currently_absorbed;
		if(unabsorbed_delay > VCPU_DELAY_THRESHOLD_NS) {
			return 1;
		} else {
			return 0;
		}
	}
	return 0;
}

void vcpu_set_absorbed(vcpu_tp vcpu, uint64_t absorbed) {
	if(vcpu != NULL) {
		vcpu->nanos_currently_absorbed = absorbed;
	}
}

uint64_t vcpu_get_delay(vcpu_tp vcpu) {
	if(vcpu != NULL) {
		return vcpu->nanos_accumulated_delay;
	}
	return 0;
}
