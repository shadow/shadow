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
    Thread* thread;
    int referenceCount;

    MAGIC_DECLARE;
};

SysCallHandler* syscallhandler_new(Host* host, Process* process,
                                   Thread* thread) {
    SysCallHandler* sys = g_new0(SysCallHandler, 1);
    MAGIC_INIT(sys);

    sys->host = host;
    host_ref(host);
    sys->process = process;
    process_ref(process);
    sys->thread = thread;
    thread_ref(thread);

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
    if (sys->thread) {
        thread_unref(sys->thread);
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

static void _syscallhandler_unblock(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    // TODO: Need to check that we're still in the same syscall from which
    // this was scheduled. For other syscalls will also need a way of
    // wiring through other return values, and probably for arranging
    // for syscall-specific cleanup (e.g. on a select timeout).
    thread_setSysCallResult(sys->thread, (SysCallReg){.as_i64 = 0});

    process_continue(sys->process);
}

static void _syscallhandler_block(SysCallHandler* sys,
                                  SimulationTime blockTime) {
    MAGIC_ASSERT(sys);
    utility_assert(blockTime > 0);

    /* ref count the objects correctly */
    syscallhandler_ref(sys);

    /* call back after the given time passes */
    Task* blockTask = task_new((TaskCallbackFunc)_syscallhandler_unblock,
                               sys,
                               NULL,
                               (TaskObjectFreeFunc)syscallhandler_unref,
                               NULL);

    /* schedule into our host event queue */
    worker_scheduleTask(blockTask, blockTime);

    /* free our ref to the task, the other ref is held by the event queue */
    task_unref(blockTask);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

static SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys,
                                              const SysCallArgs* args) {
    const struct timespec* req =
        thread_readPluginPtr(sys->thread, args->args[0].as_ptr, sizeof(*req));

    if (!(req->tv_nsec >= 0 && req->tv_nsec <= 999999999)) {
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = -EINVAL};
    }

    if (req->tv_sec == 0 && req->tv_nsec == 0) {
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                               .retval.as_i64 = 0};
    }

    /* how much simtime do we wait */
    SimulationTime sleepDelay =
        ((SimulationTime)req->tv_nsec) +
        ((SimulationTime)req->tv_sec * SIMTIME_ONE_SECOND);

    /* set up a block task in the host */
    // TODO I think this should go in process.c with the rest of the thread
    // scheduling code
    _syscallhandler_block(sys, sleepDelay);

    /* tell the thread we blocked it */
    return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
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
        return syscallhandler_##s(sys, args)
#define NATIVE(s)                                                              \
    case SYS_##s:                                                              \
        debug("native syscall %d " #s, args->number);                          \
        return (SysCallReturn){.state = SYSCALL_RETURN_NATIVE};
SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
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
