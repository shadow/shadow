/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/timer.h"
#include "main/host/process.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif

struct _SysCallHandler {
    /* We store pointers to the host, process, and thread that the syscall
     * handler is associated with. We typically need to makes calls into
     * these modules in order to handle syscalls. */
    Host* host;
    Process* process;
    Thread* thread;

    /* Timers are used to support the timerfd syscalls (man timerfd_create);
     * they are types of descriptors on which we can listen for events.
     * Here we use it to help us handling blocking syscalls that include a
     * timeout after which we should stop blocking. */
    Timer* timer;

    /* If we are currently blocking a specific syscall, i.e., waiting for
     * a socket to be readable/writable or waiting for a timeout, the
     * syscall number of that function is stored here. The value is set
     * to negative to indicate that no syscalls are currently blocked. */
    long blockedSyscallNR;

    int referenceCount;

    MAGIC_DECLARE;
};

SysCallHandler* syscallhandler_new(Host* host, Process* process,
                                   Thread* thread) {
    utility_assert(host);
    utility_assert(process);
    utility_assert(thread);

    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .host = host,
        .process = process,
        .thread = thread,
        .blockedSyscallNR = -1,
        .referenceCount = 1,
        /* Here we create the timer directly rather than going
         * through host_createDescriptor because the descriptor
         * is not being used to service a plugin syscall and it
         * should not be tracked with an fd handle. */
        .timer = timer_new(0, CLOCK_MONOTONIC, 0),
    };

    MAGIC_INIT(sys);

    host_ref(host);
    process_ref(process);
    thread_ref(thread);

    worker_countObject(OBJECT_TYPE_SYSCALL_HANDLER, COUNTER_TYPE_NEW);
    return sys;
}

static void _syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if (sys->host) {
        host_unref(sys->host);
    }
    if (sys->process) {
        process_unref(sys->process);
    }
    if (sys->thread) {
        thread_unref(sys->thread);
    }

    if (sys->timer) {
        descriptor_unref(sys->timer);
    }

    MAGIC_CLEAR(sys);
    free(sys);
    worker_countObject(OBJECT_TYPE_SYSCALL_HANDLER, COUNTER_TYPE_FREE);
}

void syscallhandler_ref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)++;
}

void syscallhandler_unref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)--;
    utility_assert(sys->referenceCount >= 0);
    if(sys->referenceCount == 0) {
        _syscallhandler_free(sys);
    }
}

/* make sure we return the 'emulated' time, and not the actual simulation clock */
static EmulatedTime _syscallhandler_getEmulatedTime() {
    return worker_getEmulatedTime();
}

static void _syscallhandler_setListenTimeout(SysCallHandler* sys,
                                             const struct timespec* timeout) {
    MAGIC_ASSERT(sys);

    /* Set a non-repeating (one-shot) timer to the given timeout.
     * A NULL timeout indicates we should turn off the timer. */
    struct itimerspec value = {
        .it_value = timeout ? *timeout : (struct timespec){0},
    };

    /* This causes us to lose the previous state of the timer. */
    gint result = timer_setTime(sys->timer, 0, &value, NULL);

    if (result != 0) {
        error("syscallhandler failed to set timeout to %lu.%09lu seconds",
              (long unsigned int)value.it_value.tv_sec,
              (long unsigned int)value.it_value.tv_nsec);
        utility_assert(result == 0);
    }
}

static void _syscallhandler_setListenTimeoutMillis(SysCallHandler* sys, gint timeout_ms) {
    struct timespec timeout = utility_timespecFromMillis((int64_t)timeout_ms);
    _syscallhandler_setListenTimeout(sys, &timeout);
}

static int _syscallhandler_isListenTimeoutPending(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    struct itimerspec value = {0};

    gint result = timer_getTime(sys->timer, &value);
    utility_assert(result == 0);

    return value.it_value.tv_sec > 0 || value.it_value.tv_nsec > 0;
}

static inline int
_syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys) {
    /* Note that the timer is "readable" if it has a positive
     * expiration count; this call does not adjust the status. */
    return timer_getExpirationCount(sys->timer) > 0;
}

static inline int _syscallhandler_wasBlocked(const SysCallHandler* sys) {
    return sys->blockedSyscallNR >= 0;
}

static int _syscallhandler_validateDescriptor(Descriptor* descriptor,
                                              DescriptorType expectedType) {
    if (descriptor) {
        DescriptorStatus status = descriptor_getStatus(descriptor);

        if (status & DS_CLOSED) {
            warning("descriptor handle '%i' is closed",
                    descriptor_getHandle(descriptor));
            return -EBADF;
        }

        DescriptorType type = descriptor_getType(descriptor);

        if (expectedType != DT_NONE && type != expectedType) {
            warning("descriptor handle '%i' is of type %i, expected type %i",
                    descriptor_getHandle(descriptor), type, expectedType);
            return -EINVAL;
        }

        return 0;
    } else {
        return -EBADF;
    }
}

static int _syscallhandler_createEpollHelper(SysCallHandler* sys, int64_t size,
                                             int64_t flags) {
    if (size <= 0 || (flags != 0 && flags != EPOLL_CLOEXEC)) {
        return -EINVAL;
    }

    Descriptor* desc = host_createDescriptor(sys->host, DT_EPOLL);
    utility_assert(desc);

    if (flags & EPOLL_CLOEXEC) {
        descriptor_addFlags(desc, EPOLL_CLOEXEC);
    }

    return descriptor_getHandle(desc);
}

static SysCallReturn _syscallhandler_pipeHelper(SysCallHandler* sys, PluginPtr pipefdPluginPtr, gint flags) {
    if (flags & O_DIRECT) {
        warning("We don't currently support pipes in 'O_DIRECT' mode.");
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -ENOTSUP};
    }

    /* Create and check the pipe descriptor. */
    Descriptor* pipeReader = host_createDescriptor(sys->host, DT_PIPE);
    utility_assert(pipeReader);
    gint errorCode = _syscallhandler_validateDescriptor(pipeReader, DT_PIPE);
    utility_assert(errorCode == 0);

    /* A pipe descriptor is actually simulated with our Channel object,
     * the other end of which will represent the write end. */
    Descriptor* pipeWriter =
        (Descriptor*)channel_getLinkedChannel((Channel*)pipeReader);
    utility_assert(pipeWriter);
    errorCode = _syscallhandler_validateDescriptor(pipeWriter, DT_PIPE);
    utility_assert(errorCode == 0);

    /* Set any options that were given. */
    if (flags & O_NONBLOCK) {
        descriptor_addFlags(pipeReader, O_NONBLOCK);
        descriptor_addFlags(pipeWriter, O_NONBLOCK);
    }
    if (flags & O_CLOEXEC) {
        descriptor_addFlags(pipeReader, O_CLOEXEC);
        descriptor_addFlags(pipeWriter, O_CLOEXEC);
    }

    /* Return the pipe fds to the caller. */
    size_t sizeNeeded = sizeof(int) * 2;
    gint* pipefd =
        thread_getWriteablePtr(sys->thread, pipefdPluginPtr, sizeNeeded);
    pipefd[0] = descriptor_getHandle(pipeReader);
    pipefd[1] = descriptor_getHandle(pipeWriter);

    debug("pipe() returning reader fd %i and writer fd %i",
          descriptor_getHandle(pipeReader), descriptor_getHandle(pipeWriter));

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

static SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys,
                                              const SysCallArgs* args) {
    /* Grab the arg from the syscall register. */
    const struct timespec* req =
        thread_getReadablePtr(sys->thread, args->args[0].as_ptr, sizeof(*req));

    /* Bounds checking. */
    if (!(req->tv_nsec >= 0 && req->tv_nsec <= 999999999)) {
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = -EINVAL};
    }

    /* Does the timeout request require us to block? */
    int requestToBlock = req->tv_sec > 0 || req->tv_nsec > 0;

    /* Did we already block? */
    int wasBlocked = _syscallhandler_wasBlocked(sys);

    if (requestToBlock && !wasBlocked) {
        /* We need to block for a while following the requested timeout. */
        _syscallhandler_setListenTimeout(sys, req);
        process_listenForStatus(
            sys->process, sys->thread, sys->timer, NULL, DS_NONE);

        /* tell the thread we blocked it */
        return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
    }

    /* If needed, verify that the timer expired correctly. */
    if (requestToBlock && wasBlocked) {
        /* Make sure we don't have a pending timer. */
        if (_syscallhandler_isListenTimeoutPending(sys)) {
            error("nanosleep unblocked but a timer is still pending.");
        }

        /* The timer must have expired. */
        if (!_syscallhandler_didListenTimeoutExpire(sys)) {
            error("nanosleep unblocked but the timer did not expire.");
        }
    }

    /* The syscall is now complete. */
    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

static SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys,
                                                  const SysCallArgs* args) {
    clockid_t clk_id = args->args[0].as_u64;
    debug("syscallhandler_clock_gettime with %d %p", clk_id,
          GUINT_TO_POINTER(args->args[1].as_ptr.val));

    struct timespec* res_timespec = thread_getWriteablePtr(
        sys->thread, args->args[1].as_ptr, sizeof(*res_timespec));

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    res_timespec->tv_sec = now / SIMTIME_ONE_SECOND;
    res_timespec->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

static SysCallReturn syscallhandler_epoll_create(SysCallHandler* sys,
                                                 const SysCallArgs* args) {
    int64_t size = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, size, 0);

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)result};
}

static SysCallReturn syscallhandler_epoll_create1(SysCallHandler* sys,
                                                  const SysCallArgs* args) {
    int64_t flags = args->args[0].as_i64;

    int result = _syscallhandler_createEpollHelper(sys, 1, flags);

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)result};
}

static SysCallReturn syscallhandler_epoll_ctl(SysCallHandler* sys,
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
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
    }

    /* It's now safe to cast. */
    Epoll* epoll = (Epoll*)descriptor;
    utility_assert(epoll);

    /* Find the child descriptor that the epoll is monitoring. */
    descriptor = host_lookupDescriptor(sys->host, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    event =
        thread_getReadablePtr(sys->thread, args->args[3].as_ptr, sizeof(*event));

    if (descriptor) {
        errorCode = epoll_control(epoll, op, descriptor, event);
    } else {
        /* child is not a shadow descriptor, check for OS file */
        gint osfd = host_getOSHandle(sys->host, fd);
        osfd = osfd >= 0 ? osfd : fd;
        errorCode = epoll_controlOS(epoll, op, osfd, event);
    }

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
}

static SysCallReturn syscallhandler_epoll_wait(SysCallHandler* sys,
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
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
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
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)nEvents};
}

static SysCallReturn syscallhandler_close(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    gint fd = (gint)args->args[0].as_i64;
    gint errorCode = 0;

    /* Check that fd is within bounds. */
    if (fd <= 0) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EBADF};
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* descriptor = host_lookupDescriptor(sys->host, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    if (descriptor && !errorCode) {
        /* Yes! Handle it in the host netstack. */
        errorCode = host_closeUser(sys->host, fd);
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
    }

    /* Check if we have a mapped os fd. This call returns -1 to
     * us if this fd does not correspond to any os-backed file
     * that Shadow created internally. */
    gint osfd = host_getOSHandle(sys->host, fd);
    if (osfd < 0) {
        /* The fd is not part of a special file that Shadow handles internally.
         * It might be a regular OS file, and should be handled natively by
         * libc. */
        return (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};
    }

    /* OK. The given FD from the plugin corresponds to a real
     * OS file that Shadow created and handles. */

    // TODO: handle special files

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
}

static SysCallReturn syscallhandler_pipe2(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(sys, args->args[0].as_ptr, (gint)args->args[1].as_i64);
}

static SysCallReturn syscallhandler_pipe(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(sys, args->args[0].as_ptr, 0);
}

static SysCallReturn syscallhandler_read(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int fd = (int)args->args[0].as_i64;
    void* buf; // args->args[1]
    size_t bufSize = (size_t)args->args[2].as_u64;

    debug("trying to read %zu bytes on fd %i", bufSize, fd);

    /* Get the descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, fd);

    // TODO: I think every read/write on FDs needs to come through shadow.
    // The following needs to change when we add file support.
    if (!desc) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_NATIVE, .retval.as_i64 = 0};
    }

    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
    }
    utility_assert(desc);

    DescriptorType dType = descriptor_getType(desc);
    gint dFlags = descriptor_getFlags(desc);

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, 1024 * 16);
    buf = thread_getWriteablePtr(sys->thread, args->args[1].as_ptr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_TIMER:
            result = timer_read((Timer*)desc, buf, sizeNeeded);
            break;
        case DT_PIPE:
            result = transport_receiveUserData(
                (Transport*)desc, buf, sizeNeeded, NULL, NULL);
            break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
        case DT_SOCKETPAIR:
        case DT_EPOLL:
        default:
            warning("write() not yet implemented for descriptor type %i",
                    (int)dType);
            result = -ENOTSUP;
            break;
    }

    if ((result == -EWOULDBLOCK || result == -EAGAIN) &&
        !(dFlags & O_NONBLOCK)) {
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_READABLE);
        return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
    }

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)result};
}

static SysCallReturn syscallhandler_write(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = (int)args->args[0].as_i64;
    const void* buf; // args->args[1]
    size_t bufSize = (size_t)args->args[2].as_u64;

    debug("trying to write %zu bytes on fd %i", bufSize, fd);

    /* Get the descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, fd);

    // TODO: I think every read/write on FDs needs to come through shadow.
    // The following needs to change when we add file support.
    if (!desc) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_NATIVE, .retval.as_i64 = 0};
    }

    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)errorCode};
    }
    utility_assert(desc);

    DescriptorType dType = descriptor_getType(desc);
    gint dFlags = descriptor_getFlags(desc);

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, 1024 * 16);
    buf = thread_getReadablePtr(sys->thread, args->args[1].as_ptr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_TIMER: result = -EINVAL; break;
        case DT_PIPE:
            result =
                transport_sendUserData((Transport*)desc, buf, sizeNeeded, 0, 0);
            break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
        case DT_SOCKETPAIR:
        case DT_EPOLL:
        default:
            warning("write() not yet implemented for descriptor type %i",
                    (int)dType);
            result = -ENOTSUP;
            break;
    }

    if ((result == -EWOULDBLOCK || result == -EAGAIN) &&
        !(dFlags & O_NONBLOCK)) {
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_WRITABLE);
        return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
    }

    return (SysCallReturn){
        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = (int64_t)result};
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

#define HANDLE(s)                                                              \
    case SYS_##s:                                                              \
        debug("handling syscall %ld " #s, args->number);                        \
        scr = syscallhandler_##s(sys, args);                                   \
        break
#define NATIVE(s)                                                              \
    case SYS_##s:                                                              \
        debug("native syscall %ld " #s, args->number);                         \
        scr = (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};                 \
        break
SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    SysCallReturn scr;

    /* Make sure that we either don't have a blocked syscall,
     * or if we blocked a syscall, then that same syscall
     * should be executed again when it becomes unblocked. */
    if (sys->blockedSyscallNR >= 0 && sys->blockedSyscallNR != args->number) {
        error("We blocked syscall number %ld but syscall number %ld "
              "is unexpectedly being invoked",
              sys->blockedSyscallNR, args->number);
    }

    switch (args->number) {
        HANDLE(clock_gettime);
        HANDLE(close);
        HANDLE(epoll_create);
        HANDLE(epoll_create1);
        HANDLE(epoll_ctl);
        HANDLE(epoll_wait);
        HANDLE(nanosleep);
        HANDLE(pipe);
        HANDLE(pipe2);
        HANDLE(read);
        HANDLE(write);

        NATIVE(access);
        NATIVE(arch_prctl);
        NATIVE(brk);
        NATIVE(execve);
        NATIVE(fstat);
        NATIVE(mmap);
        NATIVE(mprotect);
        NATIVE(munmap);
        NATIVE(openat);
        NATIVE(prlimit64);
        NATIVE(rt_sigaction);
        NATIVE(rt_sigprocmask);
        NATIVE(set_robust_list);
        NATIVE(set_tid_address);
        NATIVE(stat);

        default:
            info("unhandled syscall %ld", args->number);
            scr = (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};
            break;
    }

    /* If we are blocking, store the syscall number so we know
     * to expect the same syscall again when it unblocks. */
    if (scr.state == SYSCALL_RETURN_BLOCKED) {
        debug("syscall %ld on thread %p of process %s is blocked", args->number,
              sys->thread, process_getName(sys->process));
        sys->blockedSyscallNR = args->number;
    } else {
        /* We are not blocking anymore, clear the block timeout. */
        if (_syscallhandler_wasBlocked(sys)) {
            _syscallhandler_setListenTimeout(sys, NULL);
        }

        /* Log some debugging info. */
        if (scr.state == SYSCALL_RETURN_NATIVE) {
            debug("syscall %ld on thread %p of process %s will be handled natively",
                  args->number, sys->thread, process_getName(sys->process));
        } else {
            debug("syscall %ld on thread %p of process %s %s", args->number,
                  sys->thread, process_getName(sys->process),
                  sys->blockedSyscallNR >= 0 ? "was blocked but is now unblocked"
                                             : "completed without blocking");
        }

        /* We are no longer blocked on a syscall. */
        sys->blockedSyscallNR = -1;
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
