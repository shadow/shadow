/*
 * shd-syscall-handler.c
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */
#include "main/host/shd-syscall-handler.h"

#include <glib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "main/core/worker.h"

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

SysCallReturn syscallhandler_sleep(SysCallHandler* sys, Thread* thread,
        unsigned int sec) {
    struct timespec request;
    request.tv_sec = (time_t)sec;
    request.tv_nsec = 0;
    struct timespec remain;
    remain.tv_sec = 0;
    remain.tv_nsec = 0;

    SysCallReturn ret = syscallhandler_nanosleep(sys, thread, &request, &remain);

    /* the nanosleep call will have set block if needed */
    if(ret.block) {
        /* we need to block so the return value is irrelevant */
        return (SysCallReturn){.block = TRUE, .errnum = 0, .retval._uint = 0};
    }

    if(ret.retval._int < 0) {
        return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._uint = (uint)remain.tv_sec};
    } else {
        return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._uint = 0};
    }
}

SysCallReturn syscallhandler_usleep(SysCallHandler* sys, Thread* thread,
        unsigned int usec) {
    struct timespec request;
    request.tv_sec = (time_t)(usec / 1000000);
    request.tv_nsec = (long)((usec % 1000000) * 1000);

    SysCallReturn ret = syscallhandler_nanosleep(sys, thread, &request, NULL);

    /* the nanosleep call will have set block if needed */
    if(ret.block) {
        /* we need to block so the return value is irrelevant */
        return (SysCallReturn){.block = TRUE, .errnum = 0, .retval._int = 0};
    }

    if(ret.retval._int < 0) {
        return (SysCallReturn){.block = FALSE, .errnum = ret.errnum, .retval._int = -1};
    } else {
        return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._int = 0};
    }
}

SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys, Thread* thread,
        const struct timespec *req, struct timespec *rem) {
    utility_assert(req);

    if(req->tv_nsec > 0 || req->tv_sec > 0) {
        /* how much simtime do we wait */
        SimulationTime sleepDelay = ((SimulationTime)req->tv_nsec) +
                ((SimulationTime)req->tv_sec * SIMTIME_ONE_SECOND);

        /* set up a block task in the host */
        // TODO I think this should go in process.c with the rest of the thread scheduling code
        _syscallhandler_block(sys, thread, sleepDelay);

        /* tell the thread we blocked it */
        return (SysCallReturn){.block = TRUE, .errnum = 0, .retval._int = 0};
    } else {
        return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._int = 0};
    }
}

SysCallReturn syscallhandler_time(SysCallHandler* sys, Thread* thread,
        time_t* tloc) {
    EmulatedTime now = _syscallhandler_getEmulatedTime();

    time_t secs = (time_t) (now / SIMTIME_ONE_SECOND);
    if(tloc != NULL){
        *tloc = secs;
    }

    return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._time_t = secs};
}

SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *tp) {
    utility_assert(tp);

    EmulatedTime now = _syscallhandler_getEmulatedTime();
    tp->tv_sec = now / SIMTIME_ONE_SECOND;
    tp->tv_nsec = now % SIMTIME_ONE_SECOND;

    return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._int = 0};
}

SysCallReturn syscallhandler_clock_getres(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *res) {
    utility_assert(res);

    /* our clock has nanosecond precision */
    res->tv_sec = 0;
    res->tv_nsec = 1;

    return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._int = 0};
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

    return (SysCallReturn){.block = FALSE, .errnum = 0, .retval._int = 0};
}
