/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/futex.h"

#include <errno.h>
#include <linux/futex.h>
#include <sys/time.h>

#include "main/host/futex_table.h"
#include "main/host/futex.h"
#include "main/host/host.h"
#include "main/host/syscall/protected.h"
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
    PluginPtr uaddr2ptr = args->args[4].as_ptr; // int*
    int val3 = args->args[5].as_i64;

    const int possible_options = FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME;
    int options = futex_op & possible_options;
    int operation = futex_op & ~possible_options;

    debug("futex called with addr=%p op=%i (operation=%i and options=%i) and val=%i", 
            (void*)uaddrptr.val, futex_op, operation, options, val);
    
    // Futexes are 32 bits on all platforms
    uint32_t* faddr = (uint32_t*)uaddrptr.val;
    uint32_t expectedVal = (uint32_t)val;

    // futex addr cannot be NULL
    if(!faddr) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    // Check if we already have a futex here
    FutexTable* ftable = host_getFutexTable(sys->host);
    Futex* futex = futextable_get(ftable, faddr);

    int result = 0;

    switch(operation) {
        case FUTEX_WAIT: {
//            // TODO: does this load/compare need to be done atomically?
//            // `man 2 futex`: blocking via a futex is an atomic compare-and-block operation
//            if(*faddr != expectedVal) {
//                result = -EAGAIN;
//            } else {
//                if(!futex) {
//                    futex = futex_new(faddr);
//                    bool success = futextable_add(ftable, futex);
//                    utility_assert(success);
//                }
//
//                FutexState fstate = futex_checkState(futex, sys->thread);
//
//                // We shouldn't have gotten here if we are already waiting
//                utility_assert(fstate != FUTEX_STATE_WAITING);
//
//                if(fstate == FUTEX_STATE_TIMEDOUT) {
//                    result = -ETIMEDOUT;
//                } else if(fstate == FUTEX_STATE_WOKEUP) {
//                    result = 0;
//                } else { // FUTEX_STATE_NONE
//                    // Add our thread as a futex waiter
//                    // TODO: handle timeout option here
//                    
//                    const struct timespec* timeout; 
//                    if(timeoutptr.val) {
//                        timeout = process_getReadablePtr(sys->process, sys->thread, timeoutptr, sizeof(*timeout));
//                    } else {
//                        timeout = NULL;
//                    }
//
//                    futex_wait(futex, sys->thread, timeout);
//
//                    return (SysCallReturn){
//                        .state = SYSCALL_BLOCK,
//                        .cond = syscallcondition_new(timeout, NULL, 0)
//                    };
//                }
//            }
//            break;
        }

        case FUTEX_WAKE: {

            //break;
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
    
    return (SysCallReturn){.state = SYSCALL_NATIVE};
    //return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}
