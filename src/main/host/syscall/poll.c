/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/poll.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/status.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"

#define NANOS_PER_MILLISEC 1000000
#define MILLIS_PER_SEC 1000

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static void _syscallhandler_getPollEventsHelper(const CompatDescriptor* cdesc, struct pollfd* pfd) {
    // Handle legacy and non-legacy descriptors. This will be NULL if it's not a legacy descriptor.
    LegacyDescriptor* ldesc = compatdescriptor_asLegacy(cdesc);

    // Some logic depends on the descriptor type. USE DT_NONE for non-legacy descriptors
    // TODO: when converted to rust, we'll need to match the RegularFile type instead
    LegacyDescriptorType dType = ldesc ? descriptor_getType(ldesc) : DT_NONE;
    Status dstat = ldesc ? descriptor_getStatus(ldesc)
                         : openfile_getStatus(compatdescriptor_borrowOpenFile(cdesc));

    if (dType == DT_FILE) {
        // Rely on the kernel to poll the OS-back file
        int res = regularfile_poll((RegularFile*)ldesc, pfd);
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

        trace("poll checking fd %i", pfd->fd);

        /* Get the descriptor. */
        const CompatDescriptor* cdesc =
            process_getRegisteredCompatDescriptor(sys->process, pfd->fd);
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

        const CompatDescriptor* cdesc =
            process_getRegisteredCompatDescriptor(sys->process, pfd->fd);
        utility_assert(cdesc); // we would have returned POLLNVAL in getPollEvents

        struct epoll_event epev = {0};
        if (pfd->events & POLLIN) {
            epev.events |= EPOLLIN;
        }
        if (pfd->events & POLLOUT) {
            epev.events |= EPOLLOUT;
        }

        if (epev.events) {
            epoll_control(sys->epoll, EPOLL_CTL_ADD, pfd->fd, cdesc, &epev, sys->host);
        }
    }
}

SysCallReturn _syscallhandler_pollHelper(SysCallHandler* sys, struct pollfd* fds, nfds_t nfds,
                                         const struct timespec* timeout) {
    // Check if any of the fds have events now
    int num_ready = _syscallhandler_getPollEvents(sys, fds, nfds);

    trace("poll update: %i of %lu fds are ready", num_ready, nfds);

    // Block or not depending on the timeout values
    if (num_ready == 0) {
        bool dont_block = timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0;

        if (dont_block || _syscallhandler_wasBlocked(sys)) {
            trace("No events are ready and poll needs to return now");
            goto done;
        } else {
            trace("No events are ready and poll needs to block");

            // Our epoll will tell us when we have events
            _syscallhandler_registerPollFDs(sys, fds, nfds);

            // Block on epoll, which is readable when any fds have events
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = (LegacyDescriptor*)sys->epoll,
                                        .status = STATUS_DESCRIPTOR_READABLE};
            SysCallCondition* cond = syscallcondition_new(trigger);
            if (timeout && (timeout->tv_sec > 0 || timeout->tv_nsec > 0)) {
                syscallcondition_setTimeout(cond, sys->host,
                                            worker_getCurrentEmulatedTime() +
                                                timeout->tv_sec * SIMTIME_ONE_SECOND +
                                                timeout->tv_nsec * SIMTIME_ONE_NANOSECOND);
            }

            // We either use our timer as a timeout, or no timeout
            return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = cond, .restartable = false};
        }
    }

    // We have events now and we've already written them to fds_ptr
    trace("poll returning %i ready events now", num_ready);
done:
    // Clear epoll for the next poll
    epoll_reset(sys->epoll);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = num_ready};
}

static SysCallReturn _syscallhandler_pollHelperPluginPtr(SysCallHandler* sys, PluginPtr fds_ptr,
                                                         nfds_t nfds,
                                                         const struct timespec* timeout) {
    // Get the pollfd struct in our memory so we can read from and write to it.
    struct pollfd* fds = NULL;
    if (nfds > 0) {
        fds = process_getMutablePtr(sys->process, fds_ptr, nfds * sizeof(*fds));
        if (!fds) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
        }
    }

    return _syscallhandler_pollHelper(sys, fds, nfds, timeout);
}

static int _syscallhandler_checkPollArgs(PluginPtr fds_ptr, nfds_t nfds) {
    if (nfds > INT_MAX) {
        trace("nfds was out of range [0, INT_MAX], returning EINVAL");
        return -EINVAL;
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

    trace("poll was called with nfds=%lu and timeout=%d", nfds, timeout_millis);

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
    } else {
        struct timespec timeout =
            (struct timespec){.tv_sec = timeout_millis / MILLIS_PER_SEC,
                              .tv_nsec = (timeout_millis % MILLIS_PER_SEC) * NANOS_PER_MILLISEC};
        return _syscallhandler_pollHelperPluginPtr(sys, fds_ptr, nfds, &timeout);
    }
}

SysCallReturn syscallhandler_ppoll(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr fds_ptr = args->args[0].as_ptr; // struct pollfd*
    nfds_t nfds = args->args[1].as_u64;
    PluginPtr ts_timeout_ptr = args->args[2].as_ptr; // const struct timespec*

    trace("ppoll was called with nfds=%lu and timeout_ptr=%p", nfds, (void*)ts_timeout_ptr.val);

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
    }

    // We read the timeout struct into local memory to avoid holding a reference
    // to plugin memory. This avoids breaking Rust's rules for multiple
    // references, and sidesteps pointer aliasing issues such as fds_ptr and
    // ts_timeout_ptr overlapping.
    struct timespec ts_timeout_val;

    if (ts_timeout_ptr.val) {
        if (process_readPtr(
                sys->process, &ts_timeout_val, ts_timeout_ptr, sizeof(ts_timeout_val)) != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
        }

        // Negative time values in the struct are invalid
        if (ts_timeout_val.tv_sec < 0 || ts_timeout_val.tv_nsec < 0) {
            trace("negative timeout given in timespec arg, returning EINVAL");
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
        }
    }

    return _syscallhandler_pollHelperPluginPtr(
        sys, fds_ptr, nfds, ts_timeout_ptr.val ? &ts_timeout_val : NULL);
}
