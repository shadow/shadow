/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/poll.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/status.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "support/logger/logger.h"

#define NANOS_PER_MILLISEC 1000000
#define MILLIS_PER_SEC 1000

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static void _syscallhandler_getPollEventsHelper(CompatDescriptor* cdesc, struct pollfd* pfd) {
    // Handle legacy and non-legacy descriptors. This will be NULL if it's not a legacy descriptor.
    LegacyDescriptor* ldesc = compatdescriptor_asLegacy(cdesc);

    // Some logic depends on the descriptor type. USE DT_NONE for non-legacy descriptors
    // TODO: when converted to rust, we'll need to match the RegularFile type instead
    LegacyDescriptorType dType = ldesc ? descriptor_getType(ldesc) : DT_NONE;
    Status dstat = ldesc ? descriptor_getStatus(ldesc)
                         : posixfile_getStatus(compatdescriptor_borrowPosixFile(cdesc));

    if (dType == DT_FILE) {
        // Rely on the kernel to poll the OS-back file
        int res = file_poll((File*)ldesc, pfd);
        if (res < 0) {
            warning("Asking the kernel to poll file %i resulted in error %i: %s", pfd->fd, -res,
                    strerror(-res));
            pfd->revents |= POLLERR;
        }
    } else {
        // Figure out which events to report
        if ((dstat & STATUS_DESCRIPTOR_CLOSED) && !(dstat & STATUS_DESCRIPTOR_ACTIVE)) {
            pfd->revents |= POLLNVAL;
        }
        if ((pfd->events & POLLIN) && (dstat & STATUS_DESCRIPTOR_ACTIVE) &&
            (dstat & STATUS_DESCRIPTOR_READABLE)) {
            pfd->revents |= POLLIN;
        }
        if ((pfd->events & POLLOUT) && (dstat & STATUS_DESCRIPTOR_ACTIVE) &&
            (dstat & STATUS_DESCRIPTOR_WRITABLE)) {
            pfd->revents |= POLLOUT;
        }
    }
}

static int _syscallhandler_getPollEvents(SysCallHandler* sys, struct pollfd* fds, nfds_t nfds) {
    int num_ready = 0;

    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd* pfd = &fds[i];
        pfd->revents = 0;

        // Negative fd just means skip over this one
        if (pfd->fd < 0) {
            // Continue is ok because we have no revents to count
            continue;
        }

        debug("poll checking fd %i", pfd->fd);

        /* Get the descriptor. */
        CompatDescriptor* cdesc = process_getRegisteredCompatDescriptor(sys->process, pfd->fd);
        if (cdesc) {
            _syscallhandler_getPollEventsHelper(cdesc, pfd);
        } else {
            pfd->revents |= POLLNVAL;
        }

        // Count if we are reporting an event
        num_ready += (pfd->revents == 0) ? 0 : 1;
    }

    return num_ready;
}

static void _syscallhandler_registerPollFDs(SysCallHandler* sys, struct pollfd* fds, nfds_t nfds) {
    // Epoll should already be clear, but let's make sure
    epoll_reset(sys->epoll);

    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd* pfd = &fds[i];

        if (pfd->fd < 0) {
            continue;
        }

        CompatDescriptor* cdesc = process_getRegisteredCompatDescriptor(sys->process, pfd->fd);
        utility_assert(cdesc); // we would have returned POLLNVAL in getPollEvents

        struct epoll_event epev = {0};
        if (pfd->events & POLLIN) {
            epev.events |= EPOLLIN;
        }
        if (pfd->events & POLLOUT) {
            epev.events |= EPOLLOUT;
        }

        if (epev.events) {
            epoll_control(sys->epoll, EPOLL_CTL_ADD, pfd->fd, cdesc, &epev);
        }
    }
}

static SysCallReturn _syscallhandler_pollHelper(SysCallHandler* sys, PluginPtr fds_ptr, nfds_t nfds,
                                                const struct timespec* timeout) {
    // Get the pollfd struct in our memory
    struct pollfd* fds =
        process_getWriteablePtr(sys->process, sys->thread, fds_ptr, nfds * sizeof(*fds));

    // Check if any of the fds have events now
    int num_ready = _syscallhandler_getPollEvents(sys, fds, nfds);

    debug("poll update: %i of %lu fds are ready", num_ready, nfds);

    // Block or not depending on the timeout values
    if (num_ready == 0) {
        bool dont_block = timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0;

        if (dont_block || _syscallhandler_wasBlocked(sys)) {
            debug("No events are ready and poll needs to return now");
            goto done;
        } else {
            debug("No events are ready and poll needs to block");

            // Our epoll will tell us when we have events
            _syscallhandler_registerPollFDs(sys, fds, nfds);

            bool need_timer = timeout && (timeout->tv_sec > 0 || timeout->tv_nsec > 0);
            if (need_timer) {
                _syscallhandler_setListenTimeout(sys, timeout);
            }

            // Block on epoll, which is readable when any fds have events
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = (LegacyDescriptor*)sys->epoll,
                                        .status = STATUS_DESCRIPTOR_READABLE};

            // We either use our timer as a timeout, or no timeout
            return (SysCallReturn){
                .state = SYSCALL_BLOCK,
                .cond = syscallcondition_new(trigger, need_timer ? sys->timer : NULL)};
        }
    }

    // We have events now and we've already written them to fds_ptr
    debug("poll returning %i ready events now", num_ready);
done:
    // Clear epoll for the next poll
    epoll_reset(sys->epoll);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = num_ready};
}

static int _syscallhandler_checkPollArgs(PluginPtr fds_ptr, nfds_t nfds) {
    if (nfds >= INT_MAX) {
        debug("nfds was out of range [0, INT_MAX], returning EINVAL");
        return -EINVAL;
    } else if (!fds_ptr.val) {
        debug("fd array was null, returning EFAULT");
        return -EFAULT;
    } else {
        return 0;
    }
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_poll(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr fds_ptr = args->args[0].as_ptr; // struct pollfd*
    nfds_t nfds = args->args[1].as_u64;
    int timeout_millis = args->args[2].as_i64;

    debug("poll was called with nfds=%lu and timeout=%d", nfds, timeout_millis);

    if (nfds == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
    } else {
        struct timespec timeout =
            (struct timespec){.tv_sec = timeout_millis / MILLIS_PER_SEC,
                              .tv_nsec = (timeout_millis % MILLIS_PER_SEC) * NANOS_PER_MILLISEC};
        return _syscallhandler_pollHelper(sys, fds_ptr, nfds, &timeout);
    }
}

SysCallReturn syscallhandler_ppoll(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr fds_ptr = args->args[0].as_ptr; // struct pollfd*
    nfds_t nfds = args->args[1].as_u64;
    PluginPtr ts_timeout_ptr = args->args[2].as_ptr; // const struct timespec*

    debug("ppoll was called with nfds=%lu and timeout_ptr=%p", nfds, (void*)ts_timeout_ptr.val);

    if (nfds == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
    }

    const struct timespec* ts_timeout;

    if (!ts_timeout_ptr.val) {
        // NULL timespec is valid and means infinite timeout
        ts_timeout = NULL;
    } else {
        ts_timeout =
            process_getReadablePtr(sys->process, sys->thread, ts_timeout_ptr, sizeof(*ts_timeout));

        // Negative time values in the struct are invalid
        if (ts_timeout->tv_sec < 0 || ts_timeout->tv_nsec < 0) {
            debug("negative timeout given in timespec arg, returning EINVAL");
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
        }
    }

    return _syscallhandler_pollHelper(sys, fds_ptr, nfds, ts_timeout);
}