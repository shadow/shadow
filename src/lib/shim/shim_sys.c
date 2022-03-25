/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_sys.h"
#include "main/core/support/definitions.h" // for SIMTIME definitions
#include "main/host/syscall_numbers.h"

static EmulatedTime _shim_sys_get_time() {
    ShimShmemProcess* mem = shim_processSharedMem();

    // If that's unavailable, fail. This can happen during early init.
    if (mem == NULL) {
        return 0;
    }

    return shimshmem_getEmulatedTime(mem);
}

uint64_t shim_sys_get_simtime_nanos() { return _shim_sys_get_time() / SIMTIME_ONE_NANOSECOND; }

bool shim_sys_handle_syscall_locally(long syscall_num, long* rv, va_list args) {
    // This function is called on every syscall operation so be careful not to doing
    // anything too expensive outside of the switch cases.

    switch (syscall_num) {
        case SYS_clock_gettime: {
            EmulatedTime emulated_time = _shim_sys_get_time();
            if (emulated_time == 0) {
                // Not initialized yet.
                return false;
            }

            trace("servicing syscall %ld:clock_gettime from the shim", syscall_num);

            clockid_t clk_id = va_arg(args, clockid_t);
            struct timespec* tp = va_arg(args, struct timespec*);

            if (tp) {
                *tp = (struct timespec){
                    .tv_sec = emulated_time / SIMTIME_ONE_SECOND,
                    .tv_nsec = emulated_time % SIMTIME_ONE_SECOND,
                };
                trace("clock_gettime() successfully copied time");
                *rv = 0;
            } else {
                trace("found NULL timespec pointer in clock_gettime");
                *rv = -EFAULT;
            }

            break;
        }

        case SYS_time: {
            EmulatedTime emulated_time = _shim_sys_get_time();
            if (emulated_time == 0) {
                // Not initialized yet.
                return false;
            }
            time_t now = emulated_time / SIMTIME_ONE_SECOND;

            trace("servicing syscall %ld:time from the shim", syscall_num);

            time_t* tp = va_arg(args, time_t*);

            if (tp) {
                *tp = now;
                trace("time() successfully copied time");
            }
            *rv = now;

            break;
        }

        case SYS_gettimeofday: {
            EmulatedTime emulated_time = _shim_sys_get_time();
            if (emulated_time == 0) {
                // Not initialized yet.
                return false;
            }
            uint64_t micros = emulated_time / SIMTIME_ONE_MICROSECOND;

            trace("servicing syscall %ld:gettimeofday from the shim", syscall_num);

            struct timeval* tp = va_arg(args, struct timeval*);

            if (tp) {
                tp->tv_sec = micros / 1000000;
                tp->tv_usec = micros % 1000000;
                trace("gettimeofday() successfully copied time");
            }
            *rv = 0;

            break;
        }

        case SYS_sched_yield: {
            // Do nothing. We already yield and move time forward after some
            // number of unblocked syscalls.
            *rv = 0;
        }

        default: {
            // the syscall was not handled
            return false;
        }
    }

    uint32_t unblockedLimit = shimshmem_unblockedSyscallLimit(shim_hostSharedMem());
    if (unblockedLimit > 0) {
        ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());
        shimshmem_incrementUnblockedSyscallCount(host_lock);
        uint32_t unblockedCount = shimshmem_getUnblockedSyscallCount(host_lock);
        shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);

        // Count the syscall and check whether we ought to yield.
        trace("Unblocked syscall count=%u limit=%u", unblockedCount, unblockedLimit);
        if (unblockedCount >= unblockedLimit) {
            trace("Reached unblocked syscall limit. Yielding.");
            syscall(SYS_shadow_yield);
        }
    }

    // the syscall was handled
    return true;
}
