/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <sys/sysinfo.h>

#include "main/core/worker.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

SysCallReturn syscallhandler_sysinfo(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    PluginPtr info_ptr = args->args[0].as_ptr; // struct sysinfo*

    debug("sysinfo called");

    if (!info_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    struct sysinfo* info =
        process_getWriteablePtr(sys->process, sys->thread, info_ptr, sizeof(*info));

    if (!info) {
        error("Unable to allocate memory for sysinfo struct.");
    }

    // These values are chosen arbitrarily; we don't think it matters too much,
    // except to maintain determinism. For example, Tor make decisions about how many
    // circuits to allow to be open (and other OOM settings) based on available memory.
    info->uptime = worker_getCurrentTime() / SIMTIME_ONE_SECOND;
    info->loads[0] = 1;
    info->loads[1] = 1;
    info->loads[2] = 1;
    info->totalram = 32;
    info->freeram = 24;
    info->sharedram = 4;
    info->bufferram = 4;
    info->totalswap = 0;
    info->freeswap = 0;
    info->procs = 100;
    info->totalhigh = 4;
    info->freehigh = 3;
    info->mem_unit = 1024 * 1024 * 1024; // GiB

    return (SysCallReturn){.state = SYSCALL_DONE};
}
