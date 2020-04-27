/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_handler.h"

#include <errno.h>
#include <glib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "main/core/worker.h"
#include "main/host/process.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

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

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

static SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys,
                                              const SysCallArgs* args) {
    /* Grab the arg from the syscall register. */
    const struct timespec* req =
        thread_readPluginPtr(sys->thread, args->args[0].as_ptr, sizeof(*req));

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

        /* We are done blocking now, make sure to clear the old timeout. */
        _syscallhandler_setListenTimeout(sys, NULL);
    }

    /* The syscall is now complete. */
    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

static SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys,
                                                  const SysCallArgs* args) {
    clockid_t clk_id = args->args[0].as_u64;
    debug("syscallhandler_clock_gettime with %d %p", clk_id,
          args->args[1].as_ptr);

    struct timespec* res_timespec = thread_writePluginPtr(
        sys->thread, args->args[1].as_ptr, sizeof(*res_timespec));

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    res_timespec->tv_sec = now / SIMTIME_ONE_SECOND;
    res_timespec->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

#define HANDLE(s)                                                              \
    case SYS_##s:                                                              \
        debug("handled syscall %d " #s, args->number);                         \
        scr = syscallhandler_##s(sys, args);                                   \
        break
#define NATIVE(s)                                                              \
    case SYS_##s:                                                              \
        debug("native syscall %d " #s, args->number);                          \
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
        error("We blocked syscall number %l but syscall number %l "
              "is unexpectedly being invoked",
              sys->blockedSyscallNR, args->number);
    }

    switch (args->number) {
        HANDLE(clock_gettime);
        HANDLE(nanosleep);

        NATIVE(access);
        NATIVE(arch_prctl);
        NATIVE(brk);
        NATIVE(close);
        NATIVE(execve);
        NATIVE(fstat);
        NATIVE(mmap);
        NATIVE(mprotect);
        NATIVE(munmap);
        NATIVE(openat);
        NATIVE(prlimit64);
        NATIVE(read);
        NATIVE(rt_sigaction);
        NATIVE(rt_sigprocmask);
        NATIVE(set_robust_list);
        NATIVE(set_tid_address);
        NATIVE(stat);
        NATIVE(write);

        default:
            info("unhandled syscall %d", args->number);
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
        debug("syscall %ld on thread %p of process %s %s", args->number,
              sys->thread, process_getName(sys->process),
              sys->blockedSyscallNR >= 0 ? "is unblocked"
                                         : "completed without blocking");
        sys->blockedSyscallNR = -1;
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
