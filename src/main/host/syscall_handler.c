/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/process.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_numbers.h"
#include "main/host/syscall_types.h"
#include "main/utility/syscall.h"

static bool _countSyscalls = false;
ADD_CONFIG_HANDLER(config_getUseSyscallCounters, _countSyscalls)

const Host* _syscallhandler_getHost(const SysCallHandler* sys) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == sys->hostId);
    return host;
}

const Process* _syscallhandler_getProcess(const SysCallHandler* sys) {
    const Process* process = worker_getCurrentProcess();
    utility_debugAssert(process_getProcessID(process) == sys->processId);
    return process;
}

const Thread* _syscallhandler_getThread(const SysCallHandler* sys) {
    const Thread* thread = worker_getCurrentThread();
    utility_debugAssert(thread_getID(thread) == sys->threadId);
    return thread;
}

SysCallHandler* syscallhandler_new(HostId hostId, pid_t processId, pid_t threadId) {
    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .hostId = hostId,
        .processId = processId,
        .threadId = threadId,
        .syscall_handler_rs = rustsyscallhandler_new(hostId, processId, threadId, _countSyscalls),
        .blockedSyscallNR = -1,
        // We use an epoll object for servicing some syscalls, and so we won't
        // assign it a fd handle.
        .epoll = epoll_new(),
    };

    MAGIC_INIT(sys);

    worker_count_allocation(SysCallHandler);
    return sys;
}

void syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if (sys->syscall_handler_rs) {
        rustsyscallhandler_free(sys->syscall_handler_rs);
    }

    if (sys->epoll) {
        legacyfile_unref(sys->epoll);
    }

    MAGIC_CLEAR(sys);
    free(sys);
    worker_count_deallocation(SysCallHandler);
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_make_syscall(SysCallHandler* sys, const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    const Host* host = _syscallhandler_getHost(sys);
    const Process* process = _syscallhandler_getProcess(sys);
    const Thread* thread = _syscallhandler_getThread(sys);

    /* Make sure that we either don't have a blocked syscall,
     * or if we blocked a syscall, then that same syscall
     * should be executed again when it becomes unblocked. */
    if (sys->blockedSyscallNR >= 0 && sys->blockedSyscallNR != args->number) {
        utility_panic("We blocked syscall number %ld but syscall number %ld "
                      "is unexpectedly being invoked",
                      sys->blockedSyscallNR, args->number);
    }

    if (sys->havePendingResult) {
        // The syscall was already completed, but we delayed the response to yield the CPU.
        // Return that response now.
        trace("Returning delayed result");
        sys->havePendingResult = false;
        utility_debugAssert(sys->pendingResult.tag != SYSCALL_RETURN_BLOCK);
        sys->blockedSyscallNR = -1;
        return sys->pendingResult;
    }

    SyscallHandler* handler = sys->syscall_handler_rs;
    sys->syscall_handler_rs = NULL;
    SyscallReturn scr = rustsyscallhandler_syscall(handler, sys, args);
    sys->syscall_handler_rs = handler;

    // If the syscall would be blocked, but there's a signal pending, fail with
    // EINTR instead. The shim-side code will run the signal handlers and then
    // either return the EINTR or restart the syscall (See SA_RESTART in
    // signal(7)).
    //
    // We do this check *after* (not before) trying the syscall so that we don't
    // "interrupt" a syscall that wouldn't have blocked in the first place, or
    // that can return a "partial" result when interrupted. e.g. consider the
    // sequence:
    //
    // * Thread is blocked on reading a file descriptor.
    // * The read becomes ready and the thread is scheduled to run.
    // * The thread receives an unblocked signal.
    // * The thread runs again.
    //
    // In this scenario, the `read` call should be allowed to complete successfully.
    // from signal(7):  "If an I/O call on a slow device has already transferred
    // some data by the time it is interrupted by a signal handler, then the
    // call will return a success  status  (normally,  the  number of bytes
    // transferred)."
    if (scr.tag == SYSCALL_RETURN_BLOCK &&
        thread_unblockedSignalPending(thread, host_getShimShmemLock(host))) {
        SyscallReturnBlocked* blocked = syscallreturn_blocked(&scr);
        syscallcondition_unref(blocked->cond);
        scr = syscallreturn_makeInterrupted(blocked->restartable);
    }

    // Ensure pointers are flushed.
    if (!(scr.tag == SYSCALL_RETURN_DONE &&
          syscall_rawReturnValueToErrno(syscallreturn_done(&scr)->retval.as_i64) == 0)) {
        // The syscall didn't complete successfully; don't write back pointers.
        trace("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
        process_freePtrsWithoutFlushing(process);
    } else {
        int res = process_flushPtrs(process);
        if (res != 0) {
            panic("Flushing syscall ptrs: %s", g_strerror(-res));
        }
    }

    if (shimshmem_getModelUnblockedSyscallLatency(host_getSharedMem(host)) &&
        process_isRunning(process) &&
        (scr.tag == SYSCALL_RETURN_DONE || scr.tag == SYSCALL_RETURN_NATIVE)) {
        CSimulationTime maxUnappliedCpuLatency =
            shimshmem_maxUnappliedCpuLatency(host_getSharedMem(host));
        // Increment unblocked syscall latency, but only for
        // non-shadow-syscalls, since the latter are part of Shadow's
        // internal plumbing; they shouldn't necessarily "consume" time.
        if (!syscall_num_is_shadow(args->number)) {
            shimshmem_incrementUnappliedCpuLatency(
                host_getShimShmemLock(host),
                shimshmem_unblockedSyscallLatency(host_getSharedMem(host)));
        }
        const CSimulationTime unappliedCpuLatency =
            shimshmem_getUnappliedCpuLatency(host_getShimShmemLock(host));
        trace("Unapplied CPU latency amt=%ld max=%ld", unappliedCpuLatency, maxUnappliedCpuLatency);
        if (unappliedCpuLatency > maxUnappliedCpuLatency) {
            CEmulatedTime newTime = worker_getCurrentEmulatedTime() + unappliedCpuLatency;
            CEmulatedTime maxTime = worker_maxEventRunaheadTime(host);
            if (newTime <= maxTime) {
                trace("Reached unblocked syscall limit. Incrementing time");
                shimshmem_resetUnappliedCpuLatency(host_getShimShmemLock(host));
                worker_setCurrentEmulatedTime(newTime);
            } else {
                trace("Reached unblocked syscall limit. Yielding.");
                // Block instead, but save the result so that we can return it
                // later instead of re-executing the syscall.
                utility_debugAssert(!sys->havePendingResult);
                sys->havePendingResult = true;
                sys->pendingResult = scr;
                SysCallCondition* cond = syscallcondition_newWithAbsTimeout(newTime);

                scr = syscallreturn_makeBlocked(cond, false);
            }
        }
    }

    if (scr.tag == SYSCALL_RETURN_BLOCK) {
        /* We are blocking: store the syscall number so we know
         * to expect the same syscall again when it unblocks. */
        sys->blockedSyscallNR = args->number;
    } else {
        sys->blockedSyscallNR = -1;
    }

    return scr;
}
