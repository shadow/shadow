/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/futex.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/futex.h>
#include <stdbool.h>
#include <sys/time.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/futex.h"
#include "main/host/futex_table.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "main/utility/utility.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SyscallReturn _syscallhandler_futexWaitHelper(SyscallHandler* sys,
                                                     UntypedForeignPtr futexVPtr, int expectedVal,
                                                     UntypedForeignPtr timeoutVPtr,
                                                     TimeoutType type) {
    // This is a new wait operation on the futex for this thread.
    // Check if a timeout was given in the syscall args.
    CSimulationTime timeoutSimTime = SIMTIME_INVALID;
    if (timeoutVPtr.val) {
        struct timespec ts = {0};
        int rv = process_readPtr(rustsyscallhandler_getProcess(sys), &ts, timeoutVPtr, sizeof(ts));
        if (rv < 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
        timeoutSimTime = simtime_from_timespec(ts);
        if (timeoutSimTime == SIMTIME_INVALID) {
            return syscallreturn_makeDoneErrno(EINVAL);
        }
    }

    // Normally, the load/compare is done atomically. Since Shadow does not run multiple
    // threads from the same plugin at the same time, we do not use atomic ops.
    // `man 2 futex`: blocking via a futex is an atomic compare-and-block operation
    uint32_t futexVal;
    int result =
        process_readPtr(rustsyscallhandler_getProcess(sys), &futexVal, futexVPtr, sizeof(futexVal));
    if (result) {
        warning("Couldn't read futex address %p", (void*)futexVPtr.val);
        return syscallreturn_makeDoneErrno(-result);
    }

    trace(
        "Futex value is %" PRIu32 ", expected value is %" PRIu32, futexVal, (uint32_t)expectedVal);
    if (!rustsyscallhandler_wasBlocked(sys) && futexVal != (uint32_t)expectedVal) {
        trace("Futex values don't match, try again later");
        return syscallreturn_makeDoneErrno(EAGAIN);
    }

    // Convert the virtual ptr to a physical ptr that can uniquely identify the futex
    ManagedPhysicalMemoryAddr futexPPtr =
        process_getPhysicalAddress(rustsyscallhandler_getProcess(sys), futexVPtr);

    // Check if we already have a futex
    FutexTable* ftable = host_getFutexTable(rustsyscallhandler_getHost(sys));
    Futex* futex = futextable_get(ftable, futexPPtr);

    if (rustsyscallhandler_wasBlocked(sys)) {
        utility_debugAssert(futex != NULL);
        int result = 0;

        // We already blocked on wait, so this is either a timeout or wakeup
        if (timeoutSimTime != SIMTIME_INVALID && rustsyscallhandler_didListenTimeoutExpire(sys)) {
            // Timeout while waiting for a wakeup
            trace("Futex %p timeout out while waiting", (void*)futexPPtr.val);
            result = -ETIMEDOUT;
        } else if (thread_unblockedSignalPending(
                       rustsyscallhandler_getThread(sys),
                       host_getShimShmemLock(rustsyscallhandler_getHost(sys)))) {
            trace("Futex %p has been interrupted by a signal", (void*)futexPPtr.val);
            result = -EINTR;
        } else {
            // Proper wakeup from another thread
            trace("Futex %p has been woke up", (void*)futexPPtr.val);
            result = 0;
        }

        // Dynamically clean up the futex if needed
        if (futex_getListenerCount(futex) == 0) {
            trace("Dynamically freed a futex object for futex addr %p", (void*)futexPPtr.val);
            bool success = futextable_remove(ftable, futex);
            utility_debugAssert(success);
        }

        return syscallreturn_makeDoneI64(result);
    }

    // We'll need to block, dynamically create a futex if one does not yet exist
    if (!futex) {
        trace("Dynamically created a new futex object for futex addr %p", (void*)futexPPtr.val);
        futex = futex_new(futexPPtr);
        bool success = futextable_add(ftable, futex);
        utility_debugAssert(success);
    }

    // Now we need to block until another thread does a wake on the futex.
    trace("Futex blocking for wakeup %s timeout",
          timeoutSimTime != SIMTIME_INVALID ? "with" : "without");
    Trigger trigger =
        (Trigger){.type = TRIGGER_FUTEX, .object = futex, .status = STATUS_FUTEX_WAKEUP};
    SysCallCondition* cond = syscallcondition_new(trigger);
    if (timeoutSimTime != SIMTIME_INVALID) {
        CEmulatedTime timeoutEmulatedTime = (type == TIMEOUT_RELATIVE)
                                                ? timeoutSimTime + worker_getCurrentEmulatedTime()
                                                : timeoutSimTime;
        syscallcondition_setTimeout(cond, timeoutEmulatedTime);
    }
    return syscallreturn_makeBlocked(cond, true);
}

static SyscallReturn _syscallhandler_futexWakeHelper(SyscallHandler* sys,
                                                     UntypedForeignPtr futexVPtr, int numWakeups) {
    // Convert the virtual ptr to a physical ptr that can uniquely identify the futex
    ManagedPhysicalMemoryAddr futexPPtr =
        process_getPhysicalAddress(rustsyscallhandler_getProcess(sys), futexVPtr);

    // Lookup the futex in the futex table
    FutexTable* ftable = host_getFutexTable(rustsyscallhandler_getHost(sys));
    Futex* futex = futextable_get(ftable, futexPPtr);

    trace("Found futex %p at futex addr %p", futex, (void*)futexPPtr.val);

    unsigned int numWoken = 0;
    if (futex && numWakeups > 0) {
        trace("Futex trying to perform %i wakeups", numWakeups);
        numWoken = futex_wake(futex, (unsigned int)numWakeups);
        trace("Futex was able to perform %i/%i wakeups", numWoken, numWakeups);
    }

    return syscallreturn_makeDoneU64(numWoken);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

// TODO: currently only supports uaddr from the same virtual address space (i.e., threads)
// Support across different address spaces requires us to compute a unique id from the
// hardware address (i.e., page table and offset). This is needed, e.g., when using
// futexes across process boundaries.
SyscallReturn syscallhandler_futex(SyscallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    UntypedForeignPtr uaddrptr = args->args[0].as_ptr; // int*
    int futex_op = args->args[1].as_i64;
    int val = args->args[2].as_i64;
    UntypedForeignPtr timeoutptr = args->args[3].as_ptr; // const struct timespec*, or uint32_t
    UntypedForeignPtr uaddr2ptr = args->args[4].as_ptr;  // int*
    int val3 = args->args[5].as_i64;

    const int possible_options = FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME;
    int options = futex_op & possible_options;
    int operation = futex_op & ~possible_options;

    trace("futex called with addr=%p op=%i (operation=%i and options=%i) and val=%i",
          (void*)uaddrptr.val, futex_op, operation, options, val);

    switch (operation) {
        case FUTEX_WAIT: {
            trace("Handling FUTEX_WAIT operation %i", operation);
            return _syscallhandler_futexWaitHelper(
                sys, uaddrptr, val, timeoutptr, TIMEOUT_RELATIVE);
        }

        case FUTEX_WAKE: {
            trace("Handling FUTEX_WAKE operation %i", operation);
            return _syscallhandler_futexWakeHelper(sys, uaddrptr, val);
        }

        case FUTEX_WAIT_BITSET: {
            trace("Handling FUTEX_WAIT_BITSET operation %i bitset %d", operation, val3);
            if (val3 == FUTEX_BITSET_MATCH_ANY) {
                return _syscallhandler_futexWaitHelper(
                    sys, uaddrptr, val, timeoutptr, TIMEOUT_ABSOLUTE);
            }
            // Other bitsets not yet handled.
            break;
        }
        case FUTEX_WAKE_BITSET: {
            trace("Handling FUTEX_WAKE_BITSET operation %i bitset %d", operation, val3);
            if (val3 == FUTEX_BITSET_MATCH_ANY) {
                return _syscallhandler_futexWakeHelper(sys, uaddrptr, val);
            }
            // Other bitsets not yet handled.
            break;
        }

        case FUTEX_FD:
        case FUTEX_REQUEUE:
        case FUTEX_WAKE_OP:
        case FUTEX_LOCK_PI:
        case FUTEX_TRYLOCK_PI:
        case FUTEX_UNLOCK_PI:
        case FUTEX_CMP_REQUEUE_PI:
        case FUTEX_WAIT_REQUEUE_PI: break;
    }

    warning("Unhandled futex operation %i", operation);
    return syscallreturn_makeDoneErrno(ENOSYS);
}
