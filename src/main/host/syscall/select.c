/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/select.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/select.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/process.h"
#include "main/host/syscall/poll.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SyscallReturn _syscallhandler_select_helper(SyscallHandler* sys, int nfds,
                                                   UntypedForeignPtr readfds_ptr,
                                                   UntypedForeignPtr writefds_ptr,
                                                   UntypedForeignPtr exceptfds_ptr,
                                                   struct timespec* timeout) {
    // TODO: we could possibly reduce the max (i.e. the search space) further by checking the max fd
    // in the descriptor table.
    int nfds_max = MAX(0, MIN(nfds, FD_SETSIZE));

    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    // Get the fd_set syscall args in our memory.
    if (readfds_ptr.val &&
        process_readPtr(rustsyscallhandler_getProcess(sys), &readfds, readfds_ptr, sizeof(readfds))) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }
    if (writefds_ptr.val && process_readPtr(rustsyscallhandler_getProcess(sys), &writefds,
                                            writefds_ptr, sizeof(writefds))) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }
    if (exceptfds_ptr.val && process_readPtr(rustsyscallhandler_getProcess(sys), &exceptfds,
                                             exceptfds_ptr, sizeof(exceptfds))) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    // Translate to pollfds so we can handle with our poll() handler. We don't use epoll here
    // because that doesn't directly call file_poll() on regular files.
    struct pollfd* pfds = malloc(nfds_max * sizeof(*pfds));

    for (int i = 0; i < nfds_max; i++) {
        struct pollfd* pfd = &pfds[i];

        pfd->fd = -1; // poll will skip over this array slot
        pfd->events = 0;
        pfd->revents = 0;

        // If the syscall args were NULL, our local fd sets are zeroed.
        if (FD_ISSET(i, &readfds)) {
            trace("select wanting reads for fd %i", i);
            pfd->fd = i; // poll will process this slot
            pfd->events |= POLLIN;
        }
        if (FD_ISSET(i, &writefds)) {
            trace("select wanting writes for fd %i", i);
            pfd->fd = i; // poll will process this slot
            pfd->events |= POLLOUT;
        }
        if (FD_ISSET(i, &exceptfds)) {
            // We need poll to process this slot to check for EBADF
            trace("select wanting exceptions for fd %i", i);
            pfd->fd = i; // poll will process this slot
        }
    }

    SyscallReturn scr = _syscallhandler_pollHelper(sys, pfds, (nfds_t)nfds_max, timeout);
    if (scr.tag == SYSCALL_RETURN_BLOCK ||
        (scr.tag == SYSCALL_RETURN_DONE && syscallreturn_done(&scr)->retval.as_i64 < 0)) {
        goto done;
    }

    // Collect the pollfd results in our local fd sets
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    // From `man select`: return "the total number of bits that are set in
    // readfds, writefds, exceptfds"
    int num_set_bits = 0;
    int num_bad_fds = 0;

    // Check the pollfd results.
    for (int i = 0; i < nfds_max; i++) {
        struct pollfd* pfd = &pfds[i];

        if (pfd->fd < 0) {
            // Select didn't ask about this fd.
            continue;
        }

        // The exceptional states listed in `man select` don't apply in Shadow,
        // but POLLNVAL corresponds to an EBADF error.
        if (pfd->revents & POLLIN) {
            trace("select found fd %i readable", i);
            FD_SET(i, &readfds);
            num_set_bits++;
        }
        if (pfd->revents & POLLOUT) {
            trace("select found fd %i writeable", i);
            FD_SET(i, &writefds);
            num_set_bits++;
        }
        if (pfd->revents & POLLNVAL) {
            trace("select found bad fd %i", i);
            num_bad_fds++;
        }
    }

    trace("select set %i total bits and found %i bad fds", num_set_bits, num_bad_fds);

    // Overwrite the return val set above by poll()
    if (num_bad_fds > 0) {
        scr = syscallreturn_makeDoneErrno(EBADF);
        goto done;
    }

    // OK now we know we have success; write back the result fd sets.
    scr = syscallreturn_makeDoneI64(num_set_bits);
    if (readfds_ptr.val &&
        process_writePtr(rustsyscallhandler_getProcess(sys), readfds_ptr, &readfds, sizeof(readfds))) {
        scr = syscallreturn_makeDoneErrno(EFAULT);
        goto done;
    }
    if (writefds_ptr.val && process_writePtr(rustsyscallhandler_getProcess(sys), writefds_ptr,
                                             &writefds, sizeof(writefds))) {
        scr = syscallreturn_makeDoneErrno(EFAULT);
        goto done;
    }
    if (exceptfds_ptr.val && process_writePtr(rustsyscallhandler_getProcess(sys), exceptfds_ptr,
                                              &exceptfds, sizeof(exceptfds))) {
        scr = syscallreturn_makeDoneErrno(EFAULT);
        goto done;
    }

done:
    // Cleanup
    if (pfds) {
        free(pfds);
    }
    return scr;
}

static int _syscallhandler_check_nfds(int nfds) {
    if (nfds < 0) {
        trace("nfds was < 0, returning EINVAL");
        return -EINVAL;
    } else {
        return 0;
    }
}

static int _syscallhandler_check_timeout(const struct timespec* timeout) {
    // NULL timeout is allowed, it means block indefinitely.
    // Negative time values in the struct are invalid.
    if (timeout != NULL && (timeout->tv_sec < 0 || timeout->tv_nsec < 0)) {
        trace("negative timeout given in timespec arg, returning EINVAL");
        return -EINVAL;
    } else {
        return 0;
    }
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_select(SyscallHandler* sys, const SysCallArgs* args) {
    int nfds = args->args[0].as_i64;
    UntypedForeignPtr readfds_ptr = args->args[1].as_ptr;   // fd_set*
    UntypedForeignPtr writefds_ptr = args->args[2].as_ptr;  // fd_set*
    UntypedForeignPtr exceptfds_ptr = args->args[3].as_ptr; // fd_set*
    UntypedForeignPtr timeout_ptr = args->args[4].as_ptr;   // struct timeval*

    trace("select was called with nfds=%i, readfds=%p, writefds=%p, exceptfds=%p, and timeout=%p",
          nfds, (void*)readfds_ptr.val, (void*)writefds_ptr.val, (void*)exceptfds_ptr.val,
          (void*)timeout_ptr.val);

    int result = _syscallhandler_check_nfds(nfds);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    }

    // Use timespec to match pselect.
    struct timespec ts_timeout_val = {0};

    if (timeout_ptr.val) {
        // We read the timeout struct into local memory to avoid holding a reference
        // to plugin memory. See syscallhandler_ppoll for reasoning.
        struct timeval tv_timeout_val = {0};

        if (process_readPtr(rustsyscallhandler_getProcess(sys), &tv_timeout_val, timeout_ptr,
                            sizeof(tv_timeout_val)) != 0) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }

        // Convert timeval to timespec.
        ts_timeout_val.tv_sec = tv_timeout_val.tv_sec;
#define NSECS_PER_USEC 1000
        ts_timeout_val.tv_nsec = tv_timeout_val.tv_usec * NSECS_PER_USEC;
    }

    result = _syscallhandler_check_timeout(timeout_ptr.val ? &ts_timeout_val : NULL);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    }

    return _syscallhandler_select_helper(sys, nfds, readfds_ptr, writefds_ptr, exceptfds_ptr,
                                         timeout_ptr.val ? &ts_timeout_val : NULL);
}

SyscallReturn syscallhandler_pselect6(SyscallHandler* sys, const SysCallArgs* args) {
    int nfds = args->args[0].as_i64;
    UntypedForeignPtr readfds_ptr = args->args[1].as_ptr;   // fd_set*
    UntypedForeignPtr writefds_ptr = args->args[2].as_ptr;  // fd_set*
    UntypedForeignPtr exceptfds_ptr = args->args[3].as_ptr; // fd_set*
    UntypedForeignPtr timeout_ptr = args->args[4].as_ptr;   // const struct timespec*
    // TODO how do we handle the sigmask arg and behavior

    trace("select was called with nfds=%i, readfds=%p, writefds=%p, exceptfds=%p, and timeout=%p",
          nfds, (void*)readfds_ptr.val, (void*)writefds_ptr.val, (void*)exceptfds_ptr.val,
          (void*)timeout_ptr.val);

    int result = _syscallhandler_check_nfds(nfds);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    }

    // We read the timeout struct into local memory to avoid holding a reference
    // to plugin memory. See syscallhandler_ppoll for reasoning.
    struct timespec ts_timeout_val = {0};

    if (timeout_ptr.val) {
        if (process_readPtr(rustsyscallhandler_getProcess(sys), &ts_timeout_val, timeout_ptr,
                            sizeof(ts_timeout_val)) != 0) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }
    }

    result = _syscallhandler_check_timeout(timeout_ptr.val ? &ts_timeout_val : NULL);
    if (result != 0) {
        return syscallreturn_makeDoneErrno(-result);
    }

    return _syscallhandler_select_helper(sys, nfds, readfds_ptr, writefds_ptr, exceptfds_ptr,
                                         timeout_ptr.val ? &ts_timeout_val : NULL);
}
