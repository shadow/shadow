/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/timerfd.h"

#include <errno.h>
#include <stddef.h>
#include <sys/timerfd.h>

#include "main/core/worker.h"
#include "main/host/descriptor/timer.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateTimerHelper(SysCallHandler* sys, int tfd,
                                               Timer** timer_desc_out) {
    /* Check that fd is within bounds. */
    if (tfd <= 0) {
        info("descriptor %i out of bounds", tfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* desc = process_getRegisteredDescriptor(sys->process, tfd);
    if (desc && timer_desc_out) {
        *timer_desc_out = (Timer*)desc;
    }

    int errcode = _syscallhandler_validateDescriptor(desc, DT_TIMER);
    if (errcode) {
        info("descriptor %i is invalid", tfd);
        return errcode;
    }

    /* Now we know we have a valid timer. */
    return 0;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_timerfd_create(SysCallHandler* sys,
                                            const SysCallArgs* args) {
    int clockid = args->args[0].as_i64;
    int flags = args->args[1].as_i64;

    /* Check the clockid arg. */
    if (clockid == CLOCK_BOOTTIME || clockid == CLOCK_REALTIME_ALARM ||
        clockid == CLOCK_BOOTTIME_ALARM) {
        info("Unsupported clockid %i, we support CLOCK_REALTIME and "
             "CLOCK_MONOTONIC.",
             clockid);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    } else if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) {
        info("Unknown clockid %i.", clockid);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Create the timer and double check that it's valid. */
    Timer* timer = timer_new();
    int tfd = process_registerDescriptor(sys->process, (Descriptor*)timer);

#ifdef DEBUG
    /* This should always be a valid descriptor. */
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, NULL);
    if (errcode != 0) {
        error("Unable to find timer %i that we just created.", tfd);
    }
    utility_assert(errcode == 0);
#endif

    /* Set any options that were given. */
    if (flags & TFD_NONBLOCK) {
        descriptor_addFlags((Descriptor*)timer, O_NONBLOCK);
    }
    if (flags & TFD_CLOEXEC) {
        descriptor_addFlags((Descriptor*)timer, O_CLOEXEC);
    }

    debug("timerfd_create() returning fd %i", tfd);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = tfd};
}

SysCallReturn syscallhandler_timerfd_settime(SysCallHandler* sys,
                                             const SysCallArgs* args) {
    int tfd = args->args[0].as_i64;
    int flags = args->args[1].as_i64;
    PluginPtr newValuePtr = args->args[2].as_ptr; // const struct itimerspec*
    PluginPtr oldValuePtr = args->args[3].as_ptr; // struct itimerspec*

    /* New value should be non-null. */
    if (!newValuePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Check for valid flags. */
#ifndef TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET 0
#endif
    if (flags & ~(TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET)) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get the corresponding descriptor. */
    Timer* timer = NULL;
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, &timer);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const struct itimerspec* newValue =
        process_getReadablePtr(sys->process, sys->thread, newValuePtr, sizeof(*newValue));

    /* Old value is allowed to be null. */
    struct itimerspec* oldValue = NULL;
    if (oldValuePtr.val) {
        oldValue =
            process_getWriteablePtr(sys->process, sys->thread, oldValuePtr, sizeof(*oldValue));
    }

    /* Service the call in the timer module. */
    errcode = timer_setTime(timer, flags, newValue, oldValue);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_timerfd_gettime(SysCallHandler* sys,
                                             const SysCallArgs* args) {
    int tfd = args->args[0].as_i64;
    PluginPtr currValuePtr = args->args[1].as_ptr; // struct itimerspec*

    /* Current value should be non-null. */
    if (!currValuePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get the corresponding descriptor. */
    Timer* timer = NULL;
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, &timer);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    struct itimerspec* currValue =
        process_getWriteablePtr(sys->process, sys->thread, currValuePtr, sizeof(*currValue));

    /* Service the call in the timer module. */
    errcode = timer_getTime(timer, currValue);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}
