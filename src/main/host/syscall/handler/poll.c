/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/poll.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/status.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/syscall_condition.h"

#define NANOS_PER_MILLISEC 1000000
#define MILLIS_PER_SEC 1000

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static void _syscallhandler_getPollEventsHelper(const Descriptor* cdesc, struct pollfd* pfd) {
    // Handle legacy and non-legacy files. This will be NULL if it's not a legacy file.
    LegacyFile* ldesc = descriptor_asLegacyFile(cdesc);

    // Some logic depends on the file type. USE DT_NONE for non-legacy files
    // TODO: when converted to rust, we'll need to match the RegularFile type instead
    LegacyFileType dType = ldesc ? legacyfile_getType(ldesc) : DT_NONE;
    Status dstat =
        ldesc ? legacyfile_getStatus(ldesc) : openfile_getStatus(descriptor_borrowOpenFile(cdesc));

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
        if ((dstat & FileState_CLOSED) && !(dstat & FileState_ACTIVE)) {
            pfd->revents |= POLLNVAL;
        }
        if ((pfd->events & POLLIN) && (dstat & FileState_ACTIVE) &&
            (dstat & FileState_READABLE)) {
            pfd->revents |= POLLIN;
        }
        if ((pfd->events & POLLOUT) && (dstat & FileState_ACTIVE) &&
            (dstat & FileState_WRITABLE)) {
            pfd->revents |= POLLOUT;
        }
    }
}

static int _syscallhandler_getPollEvents(SyscallHandler* sys, struct pollfd* fds, nfds_t nfds) {
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
        const Descriptor* desc =
            thread_getRegisteredDescriptor(rustsyscallhandler_getThread(sys), pfd->fd);
        if (desc) {
            _syscallhandler_getPollEventsHelper(desc, pfd);
        } else {
            pfd->revents |= POLLNVAL;
        }

        // Count if we are reporting an event
        num_ready += (pfd->revents == 0) ? 0 : 1;
    }

    return num_ready;
}

static void _syscallhandler_registerPollFDs(SyscallHandler* sys, struct pollfd* fds, nfds_t nfds) {
    // Epoll should already be clear, but let's make sure
    epoll_reset(rustsyscallhandler_getEpoll(sys));

    for (nfds_t i = 0; i < nfds; i++) {
        struct pollfd* pfd = &fds[i];

        if (pfd->fd < 0) {
            continue;
        }

        const Descriptor* desc =
            thread_getRegisteredDescriptor(rustsyscallhandler_getThread(sys), pfd->fd);
        utility_debugAssert(desc); // we would have returned POLLNVAL in getPollEvents

        struct epoll_event epev = {0};
        if (pfd->events & POLLIN) {
            epev.events |= EPOLLIN;
        }
        if (pfd->events & POLLOUT) {
            epev.events |= EPOLLOUT;
        }

        if (epev.events) {
            epoll_control(rustsyscallhandler_getEpoll(sys), EPOLL_CTL_ADD, pfd->fd, desc, &epev,
                          rustsyscallhandler_getHost(sys));
        }
    }
}

SyscallReturn _syscallhandler_pollHelper(SyscallHandler* sys, struct pollfd* fds, nfds_t nfds,
                                         const struct timespec* timeout) {
    // Check if any of the fds have events now
    int num_ready = _syscallhandler_getPollEvents(sys, fds, nfds);

    trace("poll update: %i of %lu fds are ready", num_ready, nfds);

    // Block or not depending on the timeout values
    if (num_ready == 0) {
        bool dont_block = timeout && timeout->tv_sec == 0 && timeout->tv_nsec == 0;

        if (dont_block || rustsyscallhandler_didListenTimeoutExpire(sys)) {
            trace("No events are ready and poll needs to return now");
            goto done;
        } else if (thread_unblockedSignalPending(
                       rustsyscallhandler_getThread(sys),
                       host_getShimShmemLock(rustsyscallhandler_getHost(sys)))) {
            trace("Interrupted by a signal.");
            num_ready = -EINTR;
            goto done;
        } else {
            trace("No events are ready and poll needs to block");

            // Our epoll will tell us when we have events
            _syscallhandler_registerPollFDs(sys, fds, nfds);

            // Block on epoll, which is readable when any fds have events
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = (LegacyFile*)rustsyscallhandler_getEpoll(sys),
                                        .status = FileState_READABLE};
            SysCallCondition* cond = syscallcondition_new(trigger);
            if (timeout && (timeout->tv_sec > 0 || timeout->tv_nsec > 0)) {
                syscallcondition_setTimeout(cond, worker_getCurrentEmulatedTime() +
                                                      timeout->tv_sec * SIMTIME_ONE_SECOND +
                                                      timeout->tv_nsec * SIMTIME_ONE_NANOSECOND);
            }

            // We either use our timer as a timeout, or no timeout
            return syscallreturn_makeBlocked(cond, false);
        }
    }

    // We have events now and we've already written them to fds_ptr
    trace("poll returning %i ready events now", num_ready);
done:
    // Clear epoll for the next poll
    epoll_reset(rustsyscallhandler_getEpoll(sys));
    return syscallreturn_makeDoneI64(num_ready);
}

static SyscallReturn _syscallhandler_pollHelperUntypedForeignPtr(SyscallHandler* sys,
                                                                 UntypedForeignPtr fds_ptr,
                                                                 nfds_t nfds,
                                                                 const struct timespec* timeout) {
    // Get the pollfd struct in our memory so we can read from and write to it.
    struct pollfd* fds = NULL;
    if (nfds > 0) {
        fds = process_getMutablePtr(rustsyscallhandler_getProcess(sys), fds_ptr, nfds * sizeof(*fds));
        if (!fds) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }
    }

    return _syscallhandler_pollHelper(sys, fds, nfds, timeout);
}

static int _syscallhandler_checkPollArgs(UntypedForeignPtr fds_ptr, nfds_t nfds) {
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

SyscallReturn syscallhandler_poll(SyscallHandler* sys, const SysCallArgs* args) {
    UntypedForeignPtr fds_ptr = args->args[0].as_ptr; // struct pollfd*
    nfds_t nfds = args->args[1].as_u64;
    int timeout_millis = args->args[2].as_i64;

    trace("poll was called with nfds=%lu and timeout=%d", nfds, timeout_millis);

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    } else {
        struct timespec timeout =
            (struct timespec){.tv_sec = timeout_millis / MILLIS_PER_SEC,
                              .tv_nsec = (timeout_millis % MILLIS_PER_SEC) * NANOS_PER_MILLISEC};
        return _syscallhandler_pollHelperUntypedForeignPtr(sys, fds_ptr, nfds, &timeout);
    }
}

SyscallReturn syscallhandler_ppoll(SyscallHandler* sys, const SysCallArgs* args) {
    UntypedForeignPtr fds_ptr = args->args[0].as_ptr; // struct pollfd*
    nfds_t nfds = args->args[1].as_u64;
    UntypedForeignPtr ts_timeout_ptr = args->args[2].as_ptr; // const struct timespec*

    trace("ppoll was called with nfds=%lu and timeout_ptr=%p", nfds, (void*)ts_timeout_ptr.val);

    int result = _syscallhandler_checkPollArgs(fds_ptr, nfds);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    }

    // We read the timeout struct into local memory to avoid holding a reference
    // to plugin memory. This avoids breaking Rust's rules for multiple
    // references, and sidesteps pointer aliasing issues such as fds_ptr and
    // ts_timeout_ptr overlapping.
    struct timespec ts_timeout_val;

    if (ts_timeout_ptr.val) {
        if (process_readPtr(rustsyscallhandler_getProcess(sys), &ts_timeout_val, ts_timeout_ptr,
                            sizeof(ts_timeout_val)) != 0) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }

        // Negative time values in the struct are invalid
        if (ts_timeout_val.tv_sec < 0 || ts_timeout_val.tv_nsec < 0) {
            trace("negative timeout given in timespec arg, returning EINVAL");
            return syscallreturn_makeDoneErrno(EINVAL);
        }
    }

    return _syscallhandler_pollHelperUntypedForeignPtr(
        sys, fds_ptr, nfds, ts_timeout_ptr.val ? &ts_timeout_val : NULL);
}
