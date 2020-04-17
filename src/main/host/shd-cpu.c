/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "host/shd-cpu.h"

#include <glib.h>
#include <stddef.h>

#include "core/logger/shd-logger.h"
#include "utility/shd-utility.h"

struct _CPU {
    guint64 frequencyKHz;
    guint64 rawFrequencyKHz;
    gdouble frequencyRatio;
    SimulationTime threshold;
    SimulationTime precision;
    SimulationTime now;
    SimulationTime timeCPUAvailable;
    MAGIC_DECLARE;
};

CPU* cpu_new(guint64 frequencyKHz, guint64 rawFrequencyKHz, guint64 threshold, guint64 precision) {
    utility_assert(frequencyKHz > 0);
    CPU* cpu = g_new0(CPU, 1);
    MAGIC_INIT(cpu);

    cpu->frequencyKHz = frequencyKHz;
    cpu->threshold = threshold > 0 ? (threshold * SIMTIME_ONE_MICROSECOND) : SIMTIME_INVALID;
    cpu->precision = precision > 0 ? (precision * SIMTIME_ONE_MICROSECOND) : SIMTIME_INVALID;
    cpu->timeCPUAvailable = cpu->now = 0;

    /* get the raw speed of the experiment machine */
    if(!rawFrequencyKHz) {
        warning("unable to determine raw CPU frequency, setting %u KHz as a raw "
                "estimate, and using delay ratio of 1.0 to the simulator host", cpu->frequencyKHz);
        cpu->rawFrequencyKHz = cpu->frequencyKHz;
        cpu->frequencyRatio = 1.0;
    } else {
        cpu->rawFrequencyKHz = rawFrequencyKHz;
        cpu->frequencyRatio = (gdouble)((gdouble)cpu->rawFrequencyKHz) / ((gdouble)cpu->frequencyKHz);
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
    if(cpu->threshold == SIMTIME_INVALID) {
        return FALSE;
    } else {
        return cpu_getDelay(cpu) > 0;
    }
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

    /* first normalize the physical CPU to the virtual CPU */
    SimulationTime adjustedDelay = (SimulationTime) (cpu->frequencyRatio * delay);

    /* round the adjusted delay to the nearest precision if needed */
    if(cpu->precision != SIMTIME_INVALID) {
        SimulationTime remainder = (SimulationTime) (adjustedDelay % cpu->precision);

        /* first round down (this is also the first step to rounding up) */
        adjustedDelay -= remainder;

        /* now check if we should round up */
        SimulationTime halfPrecision = (SimulationTime) (cpu->precision / 2);
        if(remainder >= halfPrecision) {
            /* we should have rounded up, so adjust up by one interval */
            adjustedDelay += cpu->precision;
        }
    }

    cpu->timeCPUAvailable += adjustedDelay;
}
