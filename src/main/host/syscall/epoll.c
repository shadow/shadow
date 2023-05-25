/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/epoll.h"

#include <errno.h>
#include <fcntl.h>

#include <linux/time.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SyscallReturn _syscallhandler_epollWaitHelper(SysCallHandler* sys, gint epfd,
                                                     UntypedForeignPtr eventsPtr, gint maxevents,
                                                     const struct timespec* timeout) {
    /* A value of SIMTIME_INVALID (due to a NULL timeout) will indicate an indefinite timeout. */
    CSimulationTime timeout_simtime = SIMTIME_INVALID;
    if (timeout != NULL) {
        timeout_simtime = simtime_from_timespec(*timeout);
        if (timeout_simtime == SIMTIME_INVALID) {
            trace("Epoll wait with invalid timespec");
            return syscallreturn_makeDoneErrno(EINVAL);
        }
    }

    /* A value of EMUTIME_INVALID will indicate an indefinite timeout. */
    CEmulatedTime timeout_emutime = EMUTIME_INVALID;
    if (timeout_simtime != SIMTIME_INVALID) {
        timeout_emutime = emutime_add_simtime(worker_getCurrentEmulatedTime(), timeout_simtime);
        if (timeout_emutime == EMUTIME_INVALID) {
            trace("Epoll wait with invalid timespec (timeout is too large)");
            return syscallreturn_makeDoneErrno(EINVAL);
        }
    }

    /* Check input args. */
    if (maxevents <= 0) {
        trace("Maxevents %i is not greater than 0.", maxevents);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get and check the epoll descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(_syscallhandler_getProcess(sys), epfd);
    gint errorCode = _syscallhandler_validateLegacyFile(desc, DT_EPOLL);

    if (errorCode) {
        trace("Error when trying to validate epoll %i", epfd);
        return syscallreturn_makeDoneErrno(-errorCode);
    }
    utility_debugAssert(desc);

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)desc;
    utility_debugAssert(epoll);

    /* figure out how many events we actually have so we can request
     * less memory than maxevents if possible. */
    guint numReadyEvents = epoll_getNumReadyEvents(epoll);

    trace("Epoll %i says %u events are ready.", epfd, numReadyEvents);

    /* If no events are ready, our behavior depends on timeout. */
    if (numReadyEvents == 0) {
        /* Return immediately if timeout is 0 or we were already
         * blocked for a while and still have no events. */
        if (timeout_simtime == 0 || _syscallhandler_didListenTimeoutExpire(sys)) {
            trace("No events are ready on epoll %i and we need to return now", epfd);

            /* Return 0; no events are ready. */
            return syscallreturn_makeDoneI64(0);
        } else if (thread_unblockedSignalPending(
                       _syscallhandler_getThread(sys),
                       host_getShimShmemLock(_syscallhandler_getHost(sys)))) {
            return syscallreturn_makeInterrupted(false);
        } else {
            trace("No events are ready on epoll %i and we need to block", epfd);

            /* Block on epoll status. An epoll descriptor is readable when it
             * has events. */
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = (LegacyFile*)epoll,
                                        .status = STATUS_FILE_READABLE};
            SysCallCondition* cond = syscallcondition_new(trigger);

            /* Set timeout, if provided. */
            if (timeout_emutime != EMUTIME_INVALID) {
                syscallcondition_setTimeout(cond, _syscallhandler_getHost(sys), timeout_emutime);
            }

            return syscallreturn_makeBlocked(cond, false);
        }
    }

    /* We have events. Get a pointer where we should write the result. */
    guint numEventsNeeded = MIN((guint)maxevents, numReadyEvents);
    size_t sizeNeeded = sizeof(struct epoll_event) * numEventsNeeded;
    struct epoll_event* events =
        process_getWriteablePtr(_syscallhandler_getProcess(sys), eventsPtr, sizeNeeded);
    if (!events) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    /* Retrieve the events. */
    gint nEvents = 0;
    gint result = epoll_getEvents(epoll, events, numEventsNeeded, &nEvents);
    utility_debugAssert(result == 0);

    trace("Found %i ready events on epoll %i.", nEvents, epfd);

    /* Return the number of events that are ready. */
    return syscallreturn_makeDoneI64(nEvents);
}

static int _syscallhandler_createEpollHelper(SysCallHandler* sys, int64_t size,
                                             int64_t flags) {
    /* `man 2 epoll_create`: the size argument is ignored, but must be greater
     * than zero */
    if (size <= 0 || (flags != 0 && flags != EPOLL_CLOEXEC)) {
        trace("Invalid size or flags argument.");
        return -EINVAL;
    }

    int descFlags = 0;
    if (flags & EPOLL_CLOEXEC) {
        descFlags |= O_CLOEXEC;
    }

    Epoll* epolld = epoll_new();
    Descriptor* desc = descriptor_fromLegacyFile((LegacyFile*)epolld, descFlags);
    int handle = process_registerDescriptor(_syscallhandler_getProcess(sys), desc);

    return handle;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_epoll_create(SysCallHandler* sys, const SysCallArgs* args) {
    int64_t size = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, size, 0);

    return syscallreturn_makeDoneI64(result);
}

SyscallReturn syscallhandler_epoll_create1(SysCallHandler* sys, const SysCallArgs* args) {
    int64_t flags = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, 1, flags);

    return syscallreturn_makeDoneI64(result);
}

SyscallReturn syscallhandler_epoll_ctl(SysCallHandler* sys, const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    gint op = args->args[1].as_i64;
    gint fd = args->args[2].as_i64;
    UntypedForeignPtr eventPtr = args->args[3].as_ptr; // const struct epoll_event*

    /* Make sure they didn't pass a NULL pointer if EPOLL_CTL_DEL is not used. */
    if (!eventPtr.val && op != EPOLL_CTL_DEL) {
        trace("NULL event pointer passed for epoll %i", epfd);
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    /* EINVAL if fd is the same as epfd, or the requested operation op is not
     * supported by this interface */
    if (epfd == fd) {
        trace("Epoll fd %i cannot be used to wait on itself.", epfd);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get and check the epoll descriptor. */
    LegacyFile* epollDescriptor =
        process_getRegisteredLegacyFile(_syscallhandler_getProcess(sys), epfd);
    gint errorCode = _syscallhandler_validateLegacyFile(epollDescriptor, DT_EPOLL);

    if (errorCode) {
        trace("Error when trying to validate epoll %i", epfd);
        return syscallreturn_makeDoneErrno(-errorCode);
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)epollDescriptor;
    utility_debugAssert(epoll);

    /* Find the child descriptor that the epoll is monitoring. */
    const Descriptor* descriptor =
        process_getRegisteredDescriptor(_syscallhandler_getProcess(sys), fd);

    if (descriptor == NULL) {
        debug("Child %i is not a shadow descriptor", fd);
        return syscallreturn_makeDoneErrno(EBADF);
    }

    LegacyFile* legacyDescriptor = descriptor_asLegacyFile(descriptor);

    // Make sure the child is not closed only if it's a legacy file
    if (legacyDescriptor != NULL) {
        errorCode = _syscallhandler_validateLegacyFile(legacyDescriptor, DT_NONE);

        if (errorCode) {
            debug("Child %i of epoll %i is closed", fd, epfd);
            return syscallreturn_makeDoneErrno(-errorCode);
        }
    }

    const struct epoll_event* event = NULL;
    if (eventPtr.val) {
        event = process_getReadablePtr(_syscallhandler_getProcess(sys), eventPtr, sizeof(*event));
    }

    trace("Calling epoll_control on epoll %i with child %i", epfd, fd);
    errorCode = epoll_control(epoll, op, fd, descriptor, event, _syscallhandler_getHost(sys));

    return syscallreturn_makeDoneI64(errorCode);
}

SyscallReturn syscallhandler_epoll_wait(SysCallHandler* sys, const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    UntypedForeignPtr eventsPtr = args->args[1].as_ptr; // struct epoll_event*
    gint maxevents = args->args[2].as_i64;
    gint timeout_ms = args->args[3].as_i64;

    struct timespec timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_nsec = ((long)timeout_ms % 1000) * 1000 * 1000,
    };

    /* A NULL will indicate an indefinite timeout. */
    const struct timespec* timeout_ptr = &timeout;

    /* epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely". */
    if (timeout_ms < 0) {
        timeout_ptr = NULL;
    }

    return _syscallhandler_epollWaitHelper(sys, epfd, eventsPtr, maxevents, timeout_ptr);
}

SyscallReturn syscallhandler_epoll_pwait(SysCallHandler* sys, const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    UntypedForeignPtr eventsPtr = args->args[1].as_ptr; // struct epoll_event*
    gint maxevents = args->args[2].as_i64;
    gint timeout_ms = args->args[3].as_i64;
    UntypedForeignPtr sigmask = args->args[4].as_ptr;

    if (sigmask.val != 0) {
        error("epoll_pwait called with non-null sigmask, which is not yet supported by shadow; "
              "returning EINVAL");
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    struct timespec timeout = {
        .tv_sec = timeout_ms / 1000,
        .tv_nsec = ((long)timeout_ms % 1000) * 1000 * 1000,
    };

    /* A NULL will indicate an indefinite timeout. */
    const struct timespec* timeout_ptr = &timeout;

    /* epoll_wait(2): "Specifying a timeout of -1 causes epoll_wait() to block indefinitely". */
    if (timeout_ms < 0) {
        timeout_ptr = NULL;
    }

    return _syscallhandler_epollWaitHelper(sys, epfd, eventsPtr, maxevents, timeout_ptr);
}

SyscallReturn syscallhandler_epoll_pwait2(SysCallHandler* sys, const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    UntypedForeignPtr eventsPtr = args->args[1].as_ptr; // struct epoll_event*
    gint maxevents = args->args[2].as_i64;
    UntypedForeignPtr timeoutPtr = args->args[3].as_ptr; // struct timespec*
    UntypedForeignPtr sigmask = args->args[4].as_ptr;

    if (sigmask.val != 0) {
        error("epoll_pwait2 called with non-null sigmask, which is not yet supported by shadow; "
              "returning EINVAL");
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    struct timespec timeout = {0};
    const struct timespec* timeout_ptr = NULL;

    /* epoll_wait(2): "If timeout is NULL, then epoll_pwait2() can block indefinitely" */
    if (timeoutPtr.val != 0) {
        int rv =
            process_readPtr(_syscallhandler_getProcess(sys), &timeout, timeoutPtr, sizeof(timeout));

        if (rv != 0) {
            utility_alwaysAssert(rv < 0);
            return syscallreturn_makeDoneErrno(-rv);
        }

        timeout_ptr = &timeout;
    }

    return _syscallhandler_epollWaitHelper(sys, epfd, eventsPtr, maxevents, timeout_ptr);
}
