/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/epoll.h"

#include <errno.h>
#include <fcntl.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

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
    int handle = process_registerDescriptor(sys->process, desc);

    return handle;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_epoll_create(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int64_t size = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, size, 0);

    return syscallreturn_makeDoneI64(result);
}

SysCallReturn syscallhandler_epoll_create1(SysCallHandler* sys,
                                           const SysCallArgs* args) {
    int64_t flags = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, 1, flags);

    return syscallreturn_makeDoneI64(result);
}

SysCallReturn syscallhandler_epoll_ctl(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    gint op = args->args[1].as_i64;
    gint fd = args->args[2].as_i64;
    PluginPtr eventPtr = args->args[3].as_ptr; // const struct epoll_event*

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
    LegacyFile* epollDescriptor = process_getRegisteredLegacyFile(sys->process, epfd);
    gint errorCode = _syscallhandler_validateLegacyFile(epollDescriptor, DT_EPOLL);

    if (errorCode) {
        trace("Error when trying to validate epoll %i", epfd);
        return syscallreturn_makeDoneErrno(-errorCode);
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)epollDescriptor;
    utility_debugAssert(epoll);

    /* Find the child descriptor that the epoll is monitoring. */
    const Descriptor* descriptor = process_getRegisteredDescriptor(sys->process, fd);

    if (descriptor == NULL) {
        debug("Child %i is not a shadow descriptor", fd);
        return syscallreturn_makeDoneErrno(EBADF);
    }

    LegacyFile* legacyDescriptor = descriptor_asLegacyFile(descriptor);

    // Make sure the child is not closed only if it's a legacy file
    // FIXME: for now we allow child fds to be closed on EPOLL_CTL_DEL operations,
    // because libevent frequently closes before issuing the EPOLL_CTL_DEL op.
    // Once #1101 is fixed, and we correctly clean up closed watch fds, then we can
    // error out here on EPOLL_CTL_DEL ops too.
    // See: https://github.com/shadow/shadow/issues/1101
    if (legacyDescriptor != NULL && op != EPOLL_CTL_DEL) {
        errorCode = _syscallhandler_validateLegacyFile(legacyDescriptor, DT_NONE);

        if (errorCode) {
            debug("Child %i of epoll %i is closed", fd, epfd);
            return syscallreturn_makeDoneErrno(-errorCode);
        }
    }

    const struct epoll_event* event = NULL;
    if (eventPtr.val) {
        event = process_getReadablePtr(sys->process, eventPtr, sizeof(*event));
    }

    trace("Calling epoll_control on epoll %i with child %i", epfd, fd);
    errorCode = epoll_control(epoll, op, fd, descriptor, event, _syscallhandler_getHost(sys));

    return syscallreturn_makeDoneI64(errorCode);
}

SysCallReturn syscallhandler_epoll_wait(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    PluginPtr eventsPtr = args->args[1].as_ptr; // struct epoll_event*
    gint maxevents = args->args[2].as_i64;
    gint timeout_ms = args->args[3].as_i64;

    /* Check input args. */
    if (maxevents <= 0) {
        trace("Maxevents %i is not greater than 0.", maxevents);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get and check the epoll descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(sys->process, epfd);
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
        if (timeout_ms == 0 || _syscallhandler_didListenTimeoutExpire(sys)) {
            trace("No events are ready on epoll %i and we need to return now",
                  epfd);

            /* Return 0; no events are ready. */
            return syscallreturn_makeDoneI64(0);
        } else if (thread_unblockedSignalPending(
                       sys->thread, host_getShimShmemLock(_syscallhandler_getHost(sys)))) {
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
            if (timeout_ms > 0) {
                syscallcondition_setTimeout(
                    cond, _syscallhandler_getHost(sys),
                    worker_getCurrentEmulatedTime() + timeout_ms * SIMTIME_ONE_MILLISECOND);
            }

            return syscallreturn_makeBlocked(cond, false);
        }
    }

    /* We have events. Get a pointer where we should write the result. */
    guint numEventsNeeded = MIN((guint)maxevents, numReadyEvents);
    size_t sizeNeeded = sizeof(struct epoll_event) * numEventsNeeded;
    struct epoll_event* events = process_getWriteablePtr(sys->process, eventsPtr, sizeNeeded);
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

SysCallReturn syscallhandler_epoll_pwait(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr sigmask = args->args[4].as_ptr;

    if (sigmask.val != 0) {
        error("epoll_pwait called with non-null sigmask, which is not yet supported by shadow; "
              "returning ENOSYS");
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    return syscallhandler_epoll_wait(sys, args);
}
