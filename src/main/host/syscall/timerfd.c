/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/timerfd.h"

#include <errno.h>
#include <stddef.h>
#include <sys/timerfd.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateTimerHelper(SysCallHandler* sys, int tfd,
                                               TimerFd** timer_desc_out) {
    /* Check that fd is within bounds. */
    if (tfd < 0) {
        debug("descriptor %i out of bounds", tfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(sys->process, tfd);
    if (desc && timer_desc_out) {
        *timer_desc_out = (TimerFd*)desc;
    }

    int errcode = _syscallhandler_validateLegacyFile(desc, DT_TIMER);
    if (errcode) {
        debug("descriptor %i is invalid", tfd);
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
        debug("Unsupported clockid %i, we support CLOCK_REALTIME and "
              "CLOCK_MONOTONIC.",
              clockid);
        return syscallreturn_makeDoneErrno(ENOSYS);
    } else if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) {
        debug("Unknown clockid %i.", clockid);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    int descFlags = 0;
    if (flags & EPOLL_CLOEXEC) {
        descFlags |= O_CLOEXEC;
    }

    /* Create the timer and double check that it's valid. */
    TimerFd* timer = timerfd_new(thread_getHostId(sys->thread));
    Descriptor* desc = descriptor_fromLegacyFile((LegacyFile*)timer, descFlags);
    int tfd = process_registerDescriptor(sys->process, desc);

#ifdef DEBUG
    /* This should always be a valid descriptor. */
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, NULL);
    if (errcode != 0) {
        utility_panic("Unable to find timer %i that we just created.", tfd);
    }
    utility_debugAssert(errcode == 0);
#endif

    /* Set any options that were given. */
    if (flags & TFD_NONBLOCK) {
        legacyfile_addFlags((LegacyFile*)timer, O_NONBLOCK);
    }

    trace("timerfd_create() returning fd %i", tfd);

    return syscallreturn_makeDoneI64(tfd);
}

SysCallReturn syscallhandler_timerfd_settime(SysCallHandler* sys,
                                             const SysCallArgs* args) {
    int tfd = args->args[0].as_i64;
    int flags = args->args[1].as_i64;
    PluginPtr newValuePtr = args->args[2].as_ptr; // const struct itimerspec*
    PluginPtr oldValuePtr = args->args[3].as_ptr; // struct itimerspec*

    /* Check for valid flags. */
#ifndef TFD_TIMER_CANCEL_ON_SET
#define TFD_TIMER_CANCEL_ON_SET 0
#endif
    if (flags & ~(TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET)) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get the corresponding descriptor. */
    TimerFd* timer = NULL;
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, &timer);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    struct itimerspec newValue;
    if (process_readPtr(sys->process, &newValue, newValuePtr, sizeof(newValue)) != 0) {
        return syscallreturn_makeDoneErrno(EFAULT);
    };

    /* Service the call in the timer module. */
    struct itimerspec oldValue;
    errcode = timerfd_setTime(timer, _syscallhandler_getHost(sys), flags, &newValue, &oldValue);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Old value is allowed to be null. */
    if (oldValuePtr.val) {
        errcode = process_writePtr(sys->process, oldValuePtr, &oldValue, sizeof(oldValue));
        if (errcode < 0) {
            return syscallreturn_makeDoneErrno(-errcode);
        }
    }

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_timerfd_gettime(SysCallHandler* sys,
                                             const SysCallArgs* args) {
    int tfd = args->args[0].as_i64;
    PluginPtr currValuePtr = args->args[1].as_ptr; // struct itimerspec*

    /* Get the corresponding descriptor. */
    TimerFd* timer = NULL;
    int errcode = _syscallhandler_validateTimerHelper(sys, tfd, &timer);
    if (errcode != 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the timer value */
    struct itimerspec currValue;
    timerfd_getTime(timer, &currValue);

    /* Write the timer value */
    errcode = process_writePtr(sys->process, currValuePtr, &currValue, sizeof(currValue));
    if (errcode != 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(0);
}
