/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/cpu.h"

#include <glib.h>
#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/utility/utility.h"

struct _CPU {
    guint64 frequencyKHz;
    guint64 rawFrequencyKHz;
    gdouble frequencyRatio;
    CSimulationTime threshold;
    CSimulationTime precision;
    CSimulationTime now;
    CSimulationTime timeCPUAvailable;
    MAGIC_DECLARE;
};

CPU* cpu_new(guint64 frequencyKHz, guint64 rawFrequencyKHz, guint64 threshold, guint64 precision) {
    utility_debugAssert(frequencyKHz > 0);
    utility_debugAssert(rawFrequencyKHz > 0);
    CPU* cpu = g_new0(CPU, 1);
    MAGIC_INIT(cpu);

    cpu->frequencyKHz = frequencyKHz;
    cpu->threshold = threshold > 0 ? (threshold * SIMTIME_ONE_MICROSECOND) : SIMTIME_INVALID;
    cpu->precision = precision > 0 ? (precision * SIMTIME_ONE_MICROSECOND) : SIMTIME_INVALID;
    cpu->timeCPUAvailable = cpu->now = 0;

    cpu->rawFrequencyKHz = rawFrequencyKHz;
    cpu->frequencyRatio = (gdouble)((gdouble)cpu->rawFrequencyKHz) / ((gdouble)cpu->frequencyKHz);

    return cpu;
}

void cpu_free(CPU* cpu) {
    MAGIC_ASSERT(cpu);
    MAGIC_CLEAR(cpu);
    g_free(cpu);
}

CSimulationTime cpu_getDelay(CPU* cpu) {
    MAGIC_ASSERT(cpu);

    /* we only have delay if we've crossed the threshold */
    CSimulationTime builtUpDelay = cpu->timeCPUAvailable - cpu->now;
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

void cpu_updateTime(CPU* cpu, CSimulationTime now) {
    MAGIC_ASSERT(cpu);
    cpu->now = now;
    /* the time available is now if we have no delay, otherwise no change
     * this is important so that our delay is added from now or into the future
     */
    cpu->timeCPUAvailable = (CSimulationTime)MAX(cpu->timeCPUAvailable, now);
}

void cpu_addDelay(CPU* cpu, CSimulationTime delay) {
    MAGIC_ASSERT(cpu);

    /* first normalize the physical CPU to the virtual CPU */
    CSimulationTime adjustedDelay = (CSimulationTime)(cpu->frequencyRatio * delay);

    /* round the adjusted delay to the nearest precision if needed */
    if(cpu->precision != SIMTIME_INVALID) {
        CSimulationTime remainder = (CSimulationTime)(adjustedDelay % cpu->precision);

        /* first round down (this is also the first step to rounding up) */
        adjustedDelay -= remainder;

        /* now check if we should round up */
        CSimulationTime halfPrecision = (CSimulationTime)(cpu->precision / 2);
        if(remainder >= halfPrecision) {
            /* we should have rounded up, so adjust up by one interval */
            adjustedDelay += cpu->precision;
        }
    }

    cpu->timeCPUAvailable += adjustedDelay;
}
