/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_api.h"
#include "lib/shim/shim_sys.h"
#include "main/host/syscall_numbers.h"

static CEmulatedTime _shim_sys_get_time() {
    const ShimShmemHost* mem = shim_hostSharedMem();

    // If that's unavailable, fail. This shouldn't happen.
    if (mem == NULL) {
        panic("mem uninitialized");
    }

    return shimshmem_getEmulatedTime(mem);
}

uint64_t shim_sys_get_simtime_nanos() {
    return emutime_sub_emutime(_shim_sys_get_time(), EMUTIME_SIMULATION_START) /
           SIMTIME_ONE_NANOSECOND;
}

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

    char* syscallName = "<unknown>";

    switch (syscall_num) {
        case SYS_clock_gettime: {
            syscallName = "clock_gettime";

            CEmulatedTime emulated_time = _shim_sys_get_time();

            trace("servicing syscall %ld:clock_gettime from the shim", syscall_num);

            clockid_t clk_id = va_arg(args, clockid_t);
            struct timespec* tp = va_arg(args, struct timespec*);

            if (clk_id < LINUX_CLOCK_REALTIME || clk_id > LINUX_CLOCK_TAI) {
                trace("found invalid clock id %ld", (long)clk_id);
                *rv = -EINVAL;
            } else if (tp) {
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
            syscallName = "time";

            CEmulatedTime emulated_time = _shim_sys_get_time();
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
            syscallName = "gettimeofday";

            CEmulatedTime emulated_time = _shim_sys_get_time();
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
            syscallName = "sched_yield";

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

    int straceFd = shimshmem_getProcessStraceFd(shim_processSharedMem());

    if (straceFd >= 0) {
        // TODO: format the time
        uint64_t emulated_time_ms = shim_sys_get_simtime_nanos();
        pid_t tid = shimshmem_getThreadId(shim_threadSharedMem());

        bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

        char buf[100] = {0};
        int len = snprintf(buf, sizeof(buf), "%018ld [tid %d] %s(...) = %ld\n", emulated_time_ms,
                           tid, syscallName, *rv);
        len = MIN(len, sizeof(buf));

        int written = 0;
        while (1) {
            int write_rv = write(straceFd, buf + written, len - written);
            if (write_rv < 0) {
                if (errno == -EINTR || errno == -EAGAIN) {
                    continue;
                }
                warning("Unable to write to strace log");
                break;
            }
            written += write_rv;
            if (written == len) {
                break;
            }
        }

        shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
    }

    if (shimshmem_getModelUnblockedSyscallLatency(shim_hostSharedMem())) {
        ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());
        shimshmem_incrementUnappliedCpuLatency(
            host_lock, _shim_sys_latency_for_syscall(syscall_num));
        CSimulationTime unappliedCpuLatency = shimshmem_getUnappliedCpuLatency(host_lock);
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
        } else {
            shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
        }
        // Should have been released and NULLed.
        assert(!host_lock);
    }

    // the syscall was handled
    return true;
}
