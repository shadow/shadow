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
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_sys.h"
#include "main/host/syscall_numbers.h"

static CEmulatedTime _shim_sys_get_time() {
    ShimShmemHost* mem = shim_hostSharedMem();

    // If that's unavailable, fail. This can happen during early init.
    if (mem == NULL) {
        return 0;
    }

    return shimshmem_getEmulatedTime(mem);
}

uint64_t shim_sys_get_simtime_nanos() { return _shim_sys_get_time() / SIMTIME_ONE_NANOSECOND; }

static CSimulationTime _shim_sys_latency_for_syscall(long n) {
    switch (n) {
        case SYS_clock_gettime:
        case SYS_time:
        case SYS_gettimeofday:
        case SYS_getcpu:
            // This would typically be a VDSO call outside of Shadow.
            //
            // It might not be, if the caller directly used a `syscall`
            // instruction or function call, but this is unusual, and charging
            // too-little latency here shouldn't hurt much, given that its main
            // purpose is currently to escape busy loops rather than to fully
            // model CPU time.
            return shimshmem_unblockedVdsoLatency(shim_hostSharedMem());
    }
    // This would typically *not* be a VDSO call outside of Shadow, even if
    // Shadow does implement it in the shim.
    return shimshmem_unblockedSyscallLatency(shim_hostSharedMem());
}

bool shim_sys_handle_syscall_locally(long syscall_num, long* rv, va_list args) {
    // This function is called on every syscall operation so be careful not to doing
    // anything too expensive outside of the switch cases.

    switch (syscall_num) {
        case SYS_clock_gettime: {
            CEmulatedTime emulated_time = _shim_sys_get_time();
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
            CEmulatedTime emulated_time = _shim_sys_get_time();
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
            CEmulatedTime emulated_time = _shim_sys_get_time();
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

            break;
        }

        default: {
            // the syscall was not handled
            return false;
        }
    }

    if (shimshmem_getModelUnblockedSyscallLatency(shim_hostSharedMem())) {
        ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());
        shimshmem_incrementUnappliedCpuLatency(
            host_lock, _shim_sys_latency_for_syscall(syscall_num));
        CSimulationTime unappliedCpuLatency = shimshmem_getUnappliedCpuLatency(host_lock);
        // TODO: Once ptrace mode is deprecated, we can hold this lock longer to
        // avoid having to reacquire it below. We currently can't hold the lock
        // over when any syscalls would be made though, since those result in a
        // ptrace-stop returning control to shadow without giving us a chance to
        // release the lock.
        shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);

        // Count the syscall and check whether we ought to yield.
        CSimulationTime maxUnappliedCpuLatency =
            shimshmem_maxUnappliedCpuLatency(shim_hostSharedMem());
        trace("unappliedCpuLatency=%ld maxUnappliedCpuLatency=%ld", unappliedCpuLatency,
              maxUnappliedCpuLatency);
        if (unappliedCpuLatency > maxUnappliedCpuLatency) {
            // We still want to eventually return the syscall result we just
            // got, but first we yield control to Shadow so that it can move
            // time forward and reschedule this thread. This syscall itself is
            // a no-op, but the Shadow side will itself check and see that
            // unblockedCount > unblockedLimit, as it does before executing any
            // syscall.
            //
            // Since this is a Shadow syscall, it will always be passed through
            // to Shadow instead of being executed natively.

            CEmulatedTime newTime = _shim_sys_get_time() + unappliedCpuLatency;
            host_lock = shimshmemhost_lock(shim_hostSharedMem());
            CEmulatedTime maxTime = shimshmem_getMaxRunaheadTime(host_lock);
            if (newTime <= maxTime) {
                shimshmem_setEmulatedTime(shim_hostSharedMem(), newTime);
                shimshmem_resetUnappliedCpuLatency(host_lock);
                shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
                trace("Reached maxUnappliedCpuLatency. Updated time locally. (%ld ns until max)",
                      maxTime - newTime);
            } else {
                shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
                trace("Reached maxUnappliedCpuLatency. Yielding. (%ld ns past max)",
                      newTime - maxTime);
                syscall(SYS_shadow_yield);
            }
        }
        // Should have been released and NULLed.
        assert(!host_lock);
    }

    // the syscall was handled
    return true;
}
