/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/core/worker.h"

#include <glib.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/utility/utility.h"

CEmulatedTime worker_maxEventRunaheadTime(const Host* host) {
    utility_debugAssert(host);
    CEmulatedTime max = emutime_add_simtime(EMUTIME_SIMULATION_START, _worker_getRoundEndTime());

    CEmulatedTime nextEventTime = host_nextEventTime(host);
    if (nextEventTime != 0) {
        max = MIN(max, nextEventTime);
    }

    return max;
}
