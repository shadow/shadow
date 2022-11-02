/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/time.h"

#include <errno.h>
#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

/* make sure we return the 'emulated' time, and not the actual simulation clock
 */
static CEmulatedTime _syscallhandler_getEmulatedTime() { return worker_getCurrentEmulatedTime(); }

static SysCallReturn _syscallhandler_nanosleep_helper(SysCallHandler* sys, clockid_t clock_id,
                                                      int flags, PluginPtr request,
                                                      PluginPtr remainder) {
    if (clock_id == CLOCK_PROCESS_CPUTIME_ID || clock_id == CLOCK_THREAD_CPUTIME_ID) {
        warning("Unsupported clock ID %d during nanosleep", clock_id);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    if (flags != 0) {
        warning("Unsupported flag %d during nanosleep", flags);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    /* Grab the arg from the syscall register. */
    struct timespec req;
    int rv = process_readPtr(sys->process, &req, request, sizeof(req));
    if (rv < 0) {
        return syscallreturn_makeDoneErrno(-rv);
    }
    CSimulationTime reqSimTime = simtime_from_timespec(req);
    if (reqSimTime == SIMTIME_INVALID) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Does the timeout request require us to block? */
    if (reqSimTime == 0) {
        return syscallreturn_makeDoneI64(0);
    }

    /* Did we already block? */
    int wasBlocked = _syscallhandler_wasBlocked(sys);

    if (!wasBlocked) {
        SysCallCondition* cond = syscallcondition_new((Trigger){.type = TRIGGER_NONE});
        syscallcondition_setTimeout(
            cond, _syscallhandler_getHost(sys), worker_getCurrentEmulatedTime() + reqSimTime);

        /* Block the thread, unblock when the timer expires. */
        return syscallreturn_makeBlocked(cond, false);
    }

    if (!_syscallhandler_didListenTimeoutExpire(sys)) {
        // Should only happen if we were interrupted by a signal.
        utility_debugAssert(thread_unblockedSignalPending(
            sys->thread, host_getShimShmemLock(_syscallhandler_getHost(sys))));

        if (remainder.val) {
            CEmulatedTime nextExpireTime = _syscallhandler_getTimeout(sys);
            utility_debugAssert(nextExpireTime != EMUTIME_INVALID);
            utility_debugAssert(nextExpireTime >= worker_getCurrentEmulatedTime());
            CSimulationTime remainingTime = nextExpireTime - worker_getCurrentEmulatedTime();
            struct timespec timer_val = {0};
            if (!simtime_to_timespec(remainingTime, &timer_val)) {
                panic("Couldn't convert %lu", remainingTime);
            }
            int rv = process_writePtr(sys->process, remainder, &timer_val, sizeof(timer_val));
            if (rv != 0) {
                return syscallreturn_makeDoneErrno(-rv);
            }
        }
        return syscallreturn_makeInterrupted(false);
    }

    /* The syscall is now complete. */
    return syscallreturn_makeDoneI64(0);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr req = args->args[0].as_ptr;
    PluginPtr rem = args->args[1].as_ptr;
    // from man 2 nanosleep:
    //   OSIX.1 specifies that nanosleep() should measure time against the CLOCK_REALTIME clock.
    //   However, Linux measures the time using the CLOCK_MONOTONIC clock.
    return _syscallhandler_nanosleep_helper(sys, CLOCK_MONOTONIC, 0, req, rem);
}

SysCallReturn syscallhandler_clock_nanosleep(SysCallHandler* sys, const SysCallArgs* args) {
    clockid_t clock_id = args->args[0].as_i64;
    int flags = args->args[1].as_i64;
    PluginPtr req = args->args[2].as_ptr;
    PluginPtr rem = args->args[3].as_ptr;
    return _syscallhandler_nanosleep_helper(sys, clock_id, flags, req, rem);
}

SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys,
                                           const SysCallArgs* args) {
    clockid_t clk_id = args->args[0].as_i64;
    trace("syscallhandler_clock_gettime with %d %p", clk_id,
          GUINT_TO_POINTER(args->args[1].as_ptr.val));

    if (clk_id == CLOCK_PROCESS_CPUTIME_ID || clk_id == CLOCK_THREAD_CPUTIME_ID) {
        warning("Unsupported clock ID %d during gettime", clk_id);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    CEmulatedTime now = _syscallhandler_getEmulatedTime();
    struct timespec res_timespec = {
        .tv_sec = now / SIMTIME_ONE_SECOND,
        .tv_nsec = now % SIMTIME_ONE_SECOND,
    };

    int res =
        process_writePtr(sys->process, args->args[1].as_ptr, &res_timespec, sizeof(res_timespec));
    if (res) {
        return syscallreturn_makeDoneErrno(-res);
    }

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_time(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr tlocPtr = args->args[0].as_ptr; // time_t*

    time_t seconds = _syscallhandler_getEmulatedTime() / SIMTIME_ONE_SECOND;

    if (tlocPtr.val) {
        time_t* tloc = process_getWriteablePtr(sys->process, tlocPtr, sizeof(*tloc));
        if (!tloc) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }
        *tloc = seconds;
    }

    return syscallreturn_makeDoneU64(seconds);
}

SysCallReturn syscallhandler_gettimeofday(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr tvPtr = args->args[0].as_ptr; // struct timeval*

    if (tvPtr.val) {
        CEmulatedTime now = _syscallhandler_getEmulatedTime();
        struct timeval* tv = process_getWriteablePtr(sys->process, tvPtr, sizeof(*tv));
        tv->tv_sec = now / SIMTIME_ONE_SECOND;
        tv->tv_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;
    }

    return syscallreturn_makeDoneI64(0);
}