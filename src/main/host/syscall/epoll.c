/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/epoll.h"

#include <errno.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_createEpollHelper(SysCallHandler* sys, int64_t size,
                                             int64_t flags) {
    /* `man 2 epoll_create`: the size argument is ignored, but must be greater
     * than zero */
    if (size <= 0 || (flags != 0 && flags != EPOLL_CLOEXEC)) {
        return -EINVAL;
    }

    Descriptor* desc = host_createDescriptor(sys->host, DT_EPOLL);
    utility_assert(desc);
    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_EPOLL);
    utility_assert(errorCode == 0);

    if (flags & EPOLL_CLOEXEC) {
        descriptor_addFlags(desc, EPOLL_CLOEXEC);
    }

    return descriptor_getHandle(desc);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_epoll_create(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int64_t size = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, size, 0);

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = result};
}

SysCallReturn syscallhandler_epoll_create1(SysCallHandler* sys,
                                           const SysCallArgs* args) {
    int64_t flags = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, 1, flags);

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = result};
}

SysCallReturn syscallhandler_epoll_ctl(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    gint epfd = (gint)args->args[0].as_i64;
    gint op = (gint)args->args[1].as_i64;
    gint fd = (gint)args->args[2].as_i64;
    const struct epoll_event* event = NULL; // args->args[3]

    /* EINVAL if fd is the same as epfd, or the requested operation op is not
     * supported by this interface */
    if (epfd == fd) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and check the epoll descriptor. */
    Descriptor* descriptor = host_lookupDescriptor(sys->host, epfd);
    gint errorCode = _syscallhandler_validateDescriptor(descriptor, DT_EPOLL);

    if (errorCode || !descriptor) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)descriptor;
    utility_assert(epoll);

    /* Find the child descriptor that the epoll is monitoring. */
    descriptor = host_lookupDescriptor(sys->host, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    event = thread_getReadablePtr(
        sys->thread, args->args[3].as_ptr, sizeof(*event));

    if (descriptor) {
        errorCode = epoll_control(epoll, op, descriptor, event);
    } else {
        /* child is not a shadow descriptor, check for OS file */
        gint osfd = host_getOSHandle(sys->host, fd);
        osfd = osfd >= 0 ? osfd : fd;
        errorCode = epoll_controlOS(epoll, op, osfd, event);
    }

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_epoll_wait(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    gint epfd = (gint)args->args[0].as_i64;
    struct epoll_event* events = NULL; // args->args[1]
    gint maxevents = (gint)args->args[2].as_i64;
    gint timeout_ms = (gint)args->args[3].as_i64;

    /* Check input args. */
    if (maxevents <= 0) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and check the epoll descriptor. */
    Descriptor* descriptor = host_lookupDescriptor(sys->host, epfd);
    gint errorCode = _syscallhandler_validateDescriptor(descriptor, DT_EPOLL);

    if (errorCode || !descriptor) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)descriptor;
    utility_assert(epoll);

    /* figure out how many events we actually have so we can request
     * less memory than maxevents if possible. */
    guint numReadyEvents = epoll_getNumReadyEvents(epoll);

    /* If no events are ready, our behavior depends on timeout. */
    if (numReadyEvents == 0) {
        /* Return immediately if timeout is 0 or we were already
         * blocked for a while and still have no events. */
        if (timeout_ms == 0 || _syscallhandler_wasBlocked(sys)) {
            /* Return 0; no events are ready. */
            return (SysCallReturn){
                .state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
        } else {
            /* We need to block, either for timeout_ms time if it's positive,
             * or indefinitely if it's negative. */
            if (timeout_ms > 0) {
                _syscallhandler_setListenTimeoutMillis(sys, timeout_ms);
            }

            /* An epoll descriptor is readable when it has events. We either
             * use our timer as a timeout, or no timeout. */
            process_listenForStatus(sys->process, sys->thread,
                                    (timeout_ms > 0) ? sys->timer : NULL,
                                    (Descriptor*)epoll, DS_READABLE);

            return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
        }
    }

    /* We have events. Get a pointer where we should write the result. */
    guint numEventsNeeded = MIN((guint)maxevents, numReadyEvents);
    size_t sizeNeeded = sizeof(*events) * numEventsNeeded;
    events =
        thread_getWriteablePtr(sys->thread, args->args[1].as_ptr, sizeNeeded);

    /* Retrieve the events. */
    gint nEvents = 0;
    gint result = epoll_getEvents(epoll, events, numEventsNeeded, &nEvents);
    utility_assert(result == 0);

    /* Return the number of events that are ready. */
    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = nEvents};
}
