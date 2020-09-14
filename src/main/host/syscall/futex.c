/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/futex.h"

#include <errno.h>
#include <inttypes.h>
#include <linux/futex.h>
#include <sys/time.h>

#include "main/host/futex.h"
#include "main/host/futex_table.h"
#include "main/host/host.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

// TODO: currently only supports uaddr from the same virtual address space (i.e., threads)
// Support across different address spaces requires us to compute a unique id from the
// hardware address (i.e., page table and offset). This is needed, e.g., when using
// futexes across process boundaries.
SysCallReturn syscallhandler_futex(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    PluginPtr uaddrptr = args->args[0].as_ptr; // int*
    int futex_op = args->args[1].as_i64;
    int val = args->args[2].as_i64;
    PluginPtr timeoutptr = args->args[3].as_ptr; // const struct timespec*, or uint32_t
    PluginPtr uaddr2ptr = args->args[4].as_ptr;  // int*
    int val3 = args->args[5].as_i64;

    const int possible_options = FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME;
    int options = futex_op & possible_options;
    int operation = futex_op & ~possible_options;

    debug("futex called with addr=%p op=%i (operation=%i and options=%i) and val=%i",
          (void*)uaddrptr.val, futex_op, operation, options, val);

    // Futexes are 32 bits on all platforms
    void* faddr = (uint32_t*)uaddrptr.val;
    uint32_t expectedVal = (uint32_t)val;

    // futex addr cannot be NULL
    if (!faddr) {
        debug("Futex addr cannot be NULL");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    int result = 0;

    switch (operation) {
        case FUTEX_WAIT: {
            debug("Handling FUTEX_WAIT operation %i", operation);

            // This is a new wait operation on the futex for this thread.
            // Check if a timeout was given in the syscall args.
            const struct timespec* timeout;
            if (timeoutptr.val) {
                timeout =
                    process_getReadablePtr(sys->process, sys->thread, timeoutptr, sizeof(*timeout));
                // Bounds checking
                if (!(timeout->tv_nsec >= 0 && timeout->tv_nsec <= 999999999)) {
                    debug("A futex timeout was given, but the nanos value is out of range");
                    result = -EINVAL;
                    break;
                }
            } else {
                timeout = NULL;
            }

            // TODO: does this load/compare need to be done atomically?
            // `man 2 futex`: blocking via a futex is an atomic compare-and-block operation
            const uint32_t* futexVal =
                process_getReadablePtr(sys->process, sys->thread, uaddrptr, sizeof(uint32_t));

            debug("Futex value is %" PRIu32 ", expected value is %" PRIu32, futexVal, expectedVal);
            if (*futexVal != expectedVal) {
                debug("Futex values don't match, try again later");
                result = -EAGAIN;
                break;
            }

            // Check if we already have a futex
            FutexTable* ftable = host_getFutexTable(sys->host);
            Futex* futex = futextable_get(ftable, faddr);

            if (_syscallhandler_wasBlocked(sys)) {
                utility_assert(futex != NULL);

                // We already blocked on wait, so this is either a timeout or wakeup
                if (timeout && _syscallhandler_didListenTimeoutExpire(sys)) {
                    // Timeout while waiting for a wakeup
                    debug("Futex %p timeout out while waiting", faddr);
                    result = -ETIMEDOUT;
                } else {
                    // Proper wakeup from another thread
                    debug("Futex %p has been woke up", faddr);
                    result = 0;
                }

                // Dynamically clean up the futex if needed
                if (futex_getListenerCount(futex) == 0) {
                    debug("Dynamically freed a futex object for futex addr %p", faddr);
                    bool success = futextable_remove(ftable, futex);
                    utility_assert(success);
                }
                break;
            }

            // Dynamically create a futex if one does not yet exist
            if (!futex) {
                debug("Dynamically created a new futex object for futex addr %p", faddr);
                futex = futex_new(faddr);
                bool success = futextable_add(ftable, futex);
                utility_assert(success);
            }

            // Now we need to block until another thread does a wake on the futex.
            debug("Futex blocking for wakeup %s timeout", timeout ? "with" : "without");
            Trigger trigger =
                (Trigger){.type = TRIGGER_FUTEX, .object = futex, .status = STATUS_FUTEX_WAKEUP};
            return (SysCallReturn){
                .state = SYSCALL_BLOCK,
                .cond = syscallcondition_new(trigger, timeout ? sys->timer : NULL)};
        }

        case FUTEX_WAKE: {
            debug("Handling FUTEX_WAKE operation %i", operation);

            FutexTable* ftable = host_getFutexTable(sys->host);
            Futex* futex = futextable_get(ftable, faddr);

            debug("Found futex %p at futex addr %p", futex, faddr);

            if (futex && val > 0) {
                debug("Futex waking %i waiters", val);
                result = futex_wake(futex, (unsigned int)val);
            }

            break;
        }

        case FUTEX_FD:
        case FUTEX_REQUEUE:
        case FUTEX_WAKE_OP:
        case FUTEX_WAIT_BITSET:
        case FUTEX_WAKE_BITSET:
        case FUTEX_LOCK_PI:
        case FUTEX_TRYLOCK_PI:
        case FUTEX_UNLOCK_PI:
        case FUTEX_CMP_REQUEUE_PI:
        case FUTEX_WAIT_REQUEUE_PI:
        default: {
            warning("We do not yet handle futex operation %i", operation);
            result = -ENOSYS; // Invalid operation specified in futex_op
            break;
        }
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}
