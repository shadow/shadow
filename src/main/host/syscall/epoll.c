/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/epoll.h"

#include <errno.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/process.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall/protected.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_createEpollHelper(SysCallHandler* sys, int64_t size,
                                             int64_t flags) {
    /* `man 2 epoll_create`: the size argument is ignored, but must be greater
     * than zero */
    if (size <= 0 || (flags != 0 && flags != EPOLL_CLOEXEC)) {
        debug("Invalid size or flags argument.");
        return -EINVAL;
    }

    Epoll* epolld = epoll_new();
    int handle = process_registerLegacyDescriptor(sys->process, (LegacyDescriptor*)epolld);

    if (flags & EPOLL_CLOEXEC) {
        descriptor_addFlags((LegacyDescriptor*)epolld, EPOLL_CLOEXEC);
    }

    return handle;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_epoll_create(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int64_t size = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, size, 0);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}

SysCallReturn syscallhandler_epoll_create1(SysCallHandler* sys,
                                           const SysCallArgs* args) {
    int64_t flags = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, 1, flags);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}

SysCallReturn syscallhandler_epoll_ctl(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    gint op = args->args[1].as_i64;
    gint fd = args->args[2].as_i64;
    PluginPtr eventPtr = args->args[3].as_ptr; // const struct epoll_event*

    /* Make sure they didn't pass a NULL pointer if EPOLL_CTL_DEL is not used. */
    if (!eventPtr.val && op != EPOLL_CTL_DEL) {
        debug("NULL event pointer passed for epoll %i", epfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* EINVAL if fd is the same as epfd, or the requested operation op is not
     * supported by this interface */
    if (epfd == fd) {
        debug("Epoll fd %i cannot be used to wait on itself.", epfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and check the epoll descriptor. */
    LegacyDescriptor* descriptor =
        process_getRegisteredLegacyDescriptor(sys->process, epfd);
    gint errorCode = _syscallhandler_validateDescriptor(descriptor, DT_EPOLL);

    if (errorCode) {
        debug("Error when trying to validate epoll %i", epfd);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)descriptor;
    utility_assert(epoll);

    /* Find the child descriptor that the epoll is monitoring. */
    descriptor = process_getRegisteredLegacyDescriptor(sys->process, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    if (errorCode) {
        info("Child %i is not a shadow descriptor", fd);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }

    const struct epoll_event* event = NULL;
    if (eventPtr.val) {
        event = process_getReadablePtr(sys->process, sys->thread, eventPtr, sizeof(*event));
    }

    debug("Calling epoll_control on epoll %i with child %i", epfd, fd);
    errorCode = epoll_control(epoll, op, descriptor, event);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_epoll_wait(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    gint epfd = args->args[0].as_i64;
    PluginPtr eventsPtr = args->args[1].as_ptr; // struct epoll_event*
    gint maxevents = args->args[2].as_i64;
    gint timeout_ms = args->args[3].as_i64;

    /* Check input args. */
    if (maxevents <= 0) {
        debug("Maxevents %i is not greater than 0.", maxevents);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Make sure they didn't pass a NULL pointer. */
    if (!eventsPtr.val) {
        debug("NULL event pointer passed for epoll %i", epfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and check the epoll descriptor. */
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, epfd);
    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_EPOLL);

    if (errorCode) {
        debug("Error when trying to validate epoll %i", epfd);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(desc);

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)desc;
    utility_assert(epoll);

    /* figure out how many events we actually have so we can request
     * less memory than maxevents if possible. */
    guint numReadyEvents = epoll_getNumReadyEvents(epoll);

    debug("Epoll %i says %u events are ready.", epfd, numReadyEvents);

    /* If no events are ready, our behavior depends on timeout. */
    if (numReadyEvents == 0) {
        /* Return immediately if timeout is 0 or we were already
         * blocked for a while and still have no events. */
        if (timeout_ms == 0 || _syscallhandler_wasBlocked(sys)) {
            debug("No events are ready on epoll %i and we need to return now",
                  epfd);

            /* Return 0; no events are ready. */
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
        } else {
            debug("No events are ready on epoll %i and we need to block", epfd);

            /* We need to block, either for timeout_ms time if it's positive,
             * or indefinitely if it's negative. */
            if (timeout_ms > 0) {
                _syscallhandler_setListenTimeoutMillis(sys, timeout_ms);
            }

            /* Block on epoll status. An epoll descriptor is readable when it
             * has events. We either use our timer as a timeout, or no timeout. */
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = (LegacyDescriptor*)epoll,
                                        .status = STATUS_DESCRIPTOR_READABLE};

            return (SysCallReturn){
                .state = SYSCALL_BLOCK,
                .cond = syscallcondition_new(trigger, (timeout_ms > 0) ? sys->timer : NULL)};
        }
    }

    /* We have events. Get a pointer where we should write the result. */
    guint numEventsNeeded = MIN((guint)maxevents, numReadyEvents);
    size_t sizeNeeded = sizeof(struct epoll_event) * numEventsNeeded;
    struct epoll_event* events =
        process_getWriteablePtr(sys->process, sys->thread, eventsPtr, sizeNeeded);

    /* Retrieve the events. */
    gint nEvents = 0;
    gint result = epoll_getEvents(epoll, events, numEventsNeeded, &nEvents);
    utility_assert(result == 0);

    debug("Found %i ready events on epoll %i.", nEvents, epfd);

    /* Return the number of events that are ready. */
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = nEvents};
}
