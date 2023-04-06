/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/time.h"

#include <errno.h>
#include <stddef.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/worker.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SyscallReturn _syscallhandler_nanosleep_helper(SysCallHandler* sys, clockid_t clock_id,
                                                      int flags, UntypedForeignPtr request,
                                                      UntypedForeignPtr remainder) {
    if (clock_id == CLOCK_PROCESS_CPUTIME_ID || clock_id == CLOCK_THREAD_CPUTIME_ID) {
        warning("Unsupported clock ID %d during nanosleep", clock_id);
        return syscallreturn_makeDoneErrno(ENOTSUP);
    }

    if ((flags & (~TIMER_ABSTIME)) != 0) {
        warning("Unsupported flag %d during nanosleep", flags);
        return syscallreturn_makeDoneErrno(ENOTSUP);
    }

    /* Grab the arg from the syscall register. */
    struct timespec req;
    int rv = process_readPtr(_syscallhandler_getProcess(sys), &req, request, sizeof(req));
    if (rv < 0) {
        return syscallreturn_makeDoneErrno(-rv);
    }
    CSimulationTime reqSimTime = simtime_from_timespec(req);
    if (reqSimTime == SIMTIME_INVALID) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    CEmulatedTime reqEmuTime = SIMTIME_INVALID;
    if ((flags & TIMER_ABSTIME) == 0) {
        reqEmuTime = reqSimTime + worker_getCurrentEmulatedTime();
    } else {
        reqEmuTime = reqSimTime;
    }

    /* Does the timeout request require us to block? */
    if (reqEmuTime <= worker_getCurrentEmulatedTime()) {
        return syscallreturn_makeDoneI64(0);
    }

    /* Did we already block? */
    int wasBlocked = _syscallhandler_wasBlocked(sys);

    if (!wasBlocked) {
        SysCallCondition* cond = syscallcondition_new((Trigger){.type = TRIGGER_NONE});
        syscallcondition_setTimeout(cond, _syscallhandler_getHost(sys), reqEmuTime);

        /* Block the thread, unblock when the timer expires. */
        return syscallreturn_makeBlocked(cond, false);
    }

    if (!_syscallhandler_didListenTimeoutExpire(sys)) {
        // Should only happen if we were interrupted by a signal.
        utility_debugAssert(thread_unblockedSignalPending(
            _syscallhandler_getThread(sys), host_getShimShmemLock(_syscallhandler_getHost(sys))));

        if (remainder.val) {
            CEmulatedTime nextExpireTime = _syscallhandler_getTimeout(sys);
            utility_debugAssert(nextExpireTime != EMUTIME_INVALID);
            utility_debugAssert(nextExpireTime >= worker_getCurrentEmulatedTime());
            CSimulationTime remainingTime = nextExpireTime - worker_getCurrentEmulatedTime();
            struct timespec timer_val = {0};
            if (!simtime_to_timespec(remainingTime, &timer_val)) {
                panic("Couldn't convert %lu", remainingTime);
            }
            int rv = process_writePtr(
                _syscallhandler_getProcess(sys), remainder, &timer_val, sizeof(timer_val));
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

SyscallReturn syscallhandler_nanosleep(SysCallHandler* sys, const SysCallArgs* args) {
    UntypedForeignPtr req = args->args[0].as_ptr;
    UntypedForeignPtr rem = args->args[1].as_ptr;
    // from man 2 nanosleep:
    //   OSIX.1 specifies that nanosleep() should measure time against the CLOCK_REALTIME clock.
    //   However, Linux measures the time using the CLOCK_MONOTONIC clock.
    return _syscallhandler_nanosleep_helper(sys, CLOCK_MONOTONIC, 0, req, rem);
}

SyscallReturn syscallhandler_clock_nanosleep(SysCallHandler* sys, const SysCallArgs* args) {
    clockid_t clock_id = args->args[0].as_i64;
    int flags = args->args[1].as_i64;
    UntypedForeignPtr req = args->args[2].as_ptr;
    UntypedForeignPtr rem = args->args[3].as_ptr;
    return _syscallhandler_nanosleep_helper(sys, clock_id, flags, req, rem);
}

