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
static EmulatedTime _syscallhandler_getEmulatedTime() { return worker_getCurrentEmulatedTime(); }

static SysCallReturn _syscallhandler_nanosleep_helper(SysCallHandler* sys, clockid_t clock_id,
                                                      int flags, PluginPtr request,
                                                      PluginPtr remainder) {
    if (clock_id == CLOCK_PROCESS_CPUTIME_ID || clock_id == CLOCK_THREAD_CPUTIME_ID) {
        warning("Unsupported clock ID %d during nanosleep", clock_id);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    if (flags != 0) {
        warning("Unsupported flag %d during nanosleep", flags);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    /* Grab the arg from the syscall register. */
    struct timespec req;
    int rv = process_readPtr(sys->process, &req, request, sizeof(req));
    if (rv < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = rv};
    }
    SimulationTime reqSimTime = simtime_from_timespec(req);
    if (reqSimTime == SIMTIME_INVALID) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Does the timeout request require us to block? */
    if (reqSimTime == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    /* Did we already block? */
    int wasBlocked = _syscallhandler_wasBlocked(sys);

    if (!wasBlocked) {
        SysCallCondition* cond = syscallcondition_new((Trigger){.type = TRIGGER_NONE});
        syscallcondition_setTimeout(cond, sys->host, worker_getCurrentEmulatedTime() + reqSimTime);

        /* Block the thread, unblock when the timer expires. */
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = cond, .restartable = false};
    }

    SysCallCondition* cond = thread_getSysCallCondition(sys->thread);
    utility_assert(cond);
    const TimerFd* timer = syscallcondition_getTimeout(cond);
    utility_assert(timer);
    if (timerfd_getExpirationCount(timer) == 0) {
        // Should only happen if we were interrupted by a signal.
        utility_assert(
            thread_unblockedSignalPending(sys->thread, host_getShimShmemLock(sys->host)));

        struct itimerspec timer_val;
        timerfd_getTime(timer, &timer_val);
        syscallcondition_cancel(cond);

        /* Timer hasn't expired. Presumably we were interrupted. */
        if (remainder.val) {
            int rv = process_writePtr(
                sys->process, remainder, &timer_val.it_value, sizeof(timer_val.it_value));
            if (rv != 0) {
                return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = rv};
            }
        }
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINTR};
    }

    /* The syscall is now complete. */
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0, .restartable = false};
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
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[1].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    struct timespec* res_timespec =
        process_getWriteablePtr(sys->process, args->args[1].as_ptr, sizeof(*res_timespec));

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    res_timespec->tv_sec = now / SIMTIME_ONE_SECOND;
    res_timespec->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_time(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr tlocPtr = args->args[0].as_ptr; // time_t*

    time_t seconds = _syscallhandler_getEmulatedTime() / SIMTIME_ONE_SECOND;

    if (tlocPtr.val) {
        time_t* tloc = process_getWriteablePtr(sys->process, tlocPtr, sizeof(*tloc));
        *tloc = seconds;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_u64 = seconds};
}

SysCallReturn syscallhandler_gettimeofday(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr tvPtr = args->args[0].as_ptr; // struct timeval*

    if (tvPtr.val) {
        EmulatedTime now = _syscallhandler_getEmulatedTime();
        struct timeval* tv = process_getWriteablePtr(sys->process, tvPtr, sizeof(*tv));
        tv->tv_sec = now / SIMTIME_ONE_SECOND;
        tv->tv_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_sched_yield(SysCallHandler* sys, const SysCallArgs* args) {
    // Do nothing. We already yield and reschedule after some number of
    // unblocked syscalls.
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}
