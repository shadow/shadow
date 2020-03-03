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
                                       const struct timespec* req,
                                       struct timespec* rem) {
    utility_assert(req);

    if (!(req->tv_nsec >= 0 && req->tv_nsec <= 999999999)) {
        return (SysCallReturn){.have_retval = true, .retval.as_i64 = -EINVAL};
    }

    if (req->tv_sec == 0 && req->tv_nsec == 0) {
        return (SysCallReturn){.have_retval = true, .retval.as_i64 = 0};
    }

    /* how much simtime do we wait */
    SimulationTime sleepDelay =
        ((SimulationTime)req->tv_nsec) +
        ((SimulationTime)req->tv_sec * SIMTIME_ONE_SECOND);

    /* set up a block task in the host */
    // TODO I think this should go in process.c with the rest of the thread
    // scheduling code
    _syscallhandler_block(sys, thread, sleepDelay);

    /* tell the thread we blocked it */
    return (SysCallReturn){.have_retval = false};
}

SysCallReturn syscallhandler_time(SysCallHandler* sys, Thread* thread,
        time_t* tloc) {
    EmulatedTime now = _syscallhandler_getEmulatedTime();

    time_t secs = (time_t) (now / SIMTIME_ONE_SECOND);
    if(tloc != NULL){
        *tloc = secs;
    }

    return (SysCallReturn){.have_retval = true, .retval.as_i64 = secs};
}

SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *tp) {
    utility_assert(tp);

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    tp->tv_sec = now / SIMTIME_ONE_SECOND;
    tp->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.have_retval = true, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_clock_getres(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *res) {
    utility_assert(res);

    /* our clock has nanosecond precision */
    res->tv_sec = 0;
    res->tv_nsec = 1;

    return (SysCallReturn){.have_retval = true, .retval.as_i64 = 0};
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

    return (SysCallReturn){.have_retval = true, .retval.as_i64 = 0};
}

SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys, Thread* thread,
                                          const SysCallArgs* args) {
    switch (args->number) {
        case SYS_nanosleep: {
            // FIXME: arg[0] and arg[1] should be pointers to shared memory.
            // TODO(ryanwails): Add shared memory mechanism,and an API for
            // mapping shim-side pointers to shadow-side pointers.
            const struct timespec req_timespec = {
                .tv_sec = args->args[0].as_i64,
                .tv_nsec = args->args[1].as_i64};
            return syscallhandler_nanosleep(sys, thread, &req_timespec, NULL);
        }
        case SYS_clock_gettime: {
            // FIXME: args[1] should be a pointer to the timespec.
            // For now we insted write the result as i64 nanos in the returned register.
            struct timespec res_timespec;
            SysCallReturn rv = syscallhandler_clock_gettime(
                sys, thread, args->args[0].as_i64, &res_timespec);
            return (SysCallReturn) {
                .have_retval = true,
                .retval.as_i64 =
                    res_timespec.tv_sec * 1000000000LL + res_timespec.tv_nsec
            };
        }
    }
    error("unknown syscall number");
    abort();
}
