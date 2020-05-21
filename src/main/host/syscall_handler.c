/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

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
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/timer.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/epoll.h"
#include "main/host/syscall/time.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

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

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

#define HANDLE(s)                                                              \
    case SYS_##s:                                                              \
        debug("handling syscall %ld " #s, args->number);                       \
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
        HANDLE(getpid);
        HANDLE(nanosleep);
        HANDLE(pipe);
        HANDLE(pipe2);
        HANDLE(read);
        HANDLE(uname);
        HANDLE(write);

        // **************************************
        // Needed for phold, but not handled yet:
        // **************************************
        // Test coverage: test/bind
        NATIVE(bind);
        // Test coverage: test/file
        NATIVE(fstat);
        // Test coverage: test/file (via open(3))
        NATIVE(openat);
        // Test coverage: test/udp
        NATIVE(recvfrom);
        // Test coverage: test/udp
        NATIVE(sendto);
        // Test coverage: test/udp
        NATIVE(socket);

        // **************************************
        // Not handled (yet):
        // **************************************
        NATIVE(access);
        NATIVE(arch_prctl);
        NATIVE(brk);
        NATIVE(execve);
        NATIVE(mmap);
        NATIVE(mprotect);
        NATIVE(munmap);
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
        /* Log some debugging info. */
        if (scr.state == SYSCALL_RETURN_NATIVE) {
            debug("syscall %ld on thread %p of process %s will be handled "
                  "natively",
                  args->number, sys->thread, process_getName(sys->process));
        } else {
            debug("syscall %ld on thread %p of process %s %s", args->number,
                  sys->thread, process_getName(sys->process),
                  sys->blockedSyscallNR >= 0
                      ? "was blocked but is now unblocked"
                      : "completed without blocking");
        }

        /* We are no longer blocked on a syscall. */
        if (_syscallhandler_wasBlocked(sys)) {
            _syscallhandler_setListenTimeout(sys, NULL);
            sys->blockedSyscallNR = -1;
        }
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
