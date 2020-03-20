/*
 * shd-syscall-handler.c
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */
#include "main/host/shd-syscall-handler.h"

#include <errno.h>
#include <glib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "main/core/worker.h"
#include "main/host/shd-syscall-types.h"
#include "support/logger/logger.h"

struct _SysCallHandler {
    Host* host;
    Process* process;
    int referenceCount;

    MAGIC_DECLARE;
};

SysCallHandler* syscallhandler_new(Host* host, Process* process) {
    SysCallHandler* sys = g_new0(SysCallHandler, 1);
    MAGIC_INIT(sys);

    sys->host = host;
    host_ref(host);
    sys->process = process;
    process_ref(process);

    sys->referenceCount = 1;

    return sys;
}

static void _syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if(sys->host) {
        host_unref(sys->host);
    }
    if(sys->process) {
        process_unref(sys->process);
    }

    MAGIC_CLEAR(sys);
    g_free(sys);
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

static void _syscallhandler_unblock(SysCallHandler* sys, Thread* thread) {
    MAGIC_ASSERT(sys);
    // TODO: Need to check that we're still in the same syscall from which
    // this was scheduled. For other syscalls will also need a way of
    // wiring through other return values, and probably for arranging
    // for syscall-specific cleanup (e.g. on a select timeout).
    thread_setSysCallResult(thread, (SysCallReg){.as_i64 = 0});

    process_continue(sys->process);
}

static void _syscallhandler_block(SysCallHandler* sys, Thread* thread, SimulationTime blockTime) {
    MAGIC_ASSERT(sys);
    utility_assert(blockTime > 0);

    /* ref count the objects correctly */
    syscallhandler_ref(sys);
    if(thread) {
        thread_ref(thread);
    }

    /* call back after the given time passes */
    Task* blockTask = task_new((TaskCallbackFunc)_syscallhandler_unblock, sys, thread,
            (TaskObjectFreeFunc)syscallhandler_unref, (TaskArgumentFreeFunc)thread_unref);

    /* schedule into our host event queue */
    worker_scheduleTask(blockTask, blockTime);

    /* free our ref to the task, the other ref is held by the event queue */
    task_unref(blockTask);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys, Thread* thread,
                                       const SysCallArgs* args) {
    struct timespec req;
    if (process_getInterposeMethod(sys->process) == INTERPOSE_PRELOAD) {
        // FIXME: shim path FIXME: arg[0] and arg[1] should be pointers to
        // shared memory.  TODO(ryanwails): Add shared memory mechanism,and an
        // API for mapping shim-side pointers to shadow-side pointers.
        req = (struct timespec){.tv_sec = args->args[0].as_i64,
                                .tv_nsec = args->args[1].as_i64};
    } else {
        thread_memcpyToShadow(thread, &req, args->args[0].as_ptr, sizeof(req));
    }

    if (!(req.tv_nsec >= 0 && req.tv_nsec <= 999999999)) {
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = -EINVAL};
    }

    if (req.tv_sec == 0 && req.tv_nsec == 0) {
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = 0};
    }

    /* how much simtime do we wait */
    SimulationTime sleepDelay =
        ((SimulationTime)req.tv_nsec) +
        ((SimulationTime)req.tv_sec * SIMTIME_ONE_SECOND);

    /* set up a block task in the host */
    // TODO I think this should go in process.c with the rest of the thread
    // scheduling code
    _syscallhandler_block(sys, thread, sleepDelay);

    /* tell the thread we blocked it */
    return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
}

SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys, Thread* thread,
                                           const SysCallArgs* args) {
    clockid_t clk_id = args->args[0].as_u64;
    struct timespec res_timespec = {};
    debug("syscallhandler_clock_gettime with %d %p", clk_id,
          args->args[1].as_ptr);

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    res_timespec.tv_sec = now / SIMTIME_ONE_SECOND;
    res_timespec.tv_nsec = now % SIMTIME_ONE_SECOND;

    if (process_getInterposeMethod(sys->process) == INTERPOSE_PRELOAD) {
        // FIXME: shim path
        // FIXME: args[1] should be a pointer to the timespec.
        // For now we instead write the result as i64 nanos in the
        // returned register.
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 =
                                   res_timespec.tv_sec * 1000000000LL +
                                   res_timespec.tv_nsec};
    } else {
        thread_memcpyToPlugin(thread, args->args[1].as_ptr, &res_timespec,
                              sizeof(res_timespec));
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = 0};
    }
}

SysCallReturn syscallhandler_clock_getres(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *res) {
    utility_assert(res);

    /* our clock has nanosecond precision */
    res->tv_sec = 0;
    res->tv_nsec = 1;

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_gettimeofday(SysCallHandler* sys, Thread* thread,
        struct timeval *tv, struct timezone *tz) {
    utility_assert(tv);

    EmulatedTime now = _syscallhandler_getEmulatedTime();

    EmulatedTime sec = now / (EmulatedTime)SIMTIME_ONE_SECOND;
    EmulatedTime usec = (now - (sec*(EmulatedTime)SIMTIME_ONE_SECOND)) / (EmulatedTime)SIMTIME_ONE_MICROSECOND;

    utility_assert(usec < (EmulatedTime)1000000);

    tv->tv_sec = (time_t)sec;
    tv->tv_usec = (suseconds_t)usec;

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = 0};
}

#define HANDLE(s)                                                              \
    case SYS_##s:                                                              \
        debug("handled syscall %d " #s, args->number);                         \
        return syscallhandler_##s(sys, thread, args)
#define NATIVE(s)                                                              \
    case SYS_##s:                                                              \
        debug("native syscall %d " #s, args->number);                          \
        return (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};
SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys, Thread* thread,
                                          const SysCallArgs* args) {
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
            return (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};
    }
}
#undef NATIVE
#undef HANDLE
