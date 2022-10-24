/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/thread.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_event.h"
#include "main/core/worker.h"
#include "main/host/managed_thread.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/utility/syscall.h"

struct _Thread {
    // For safe down-casting. Set and checked by child class.
    int type_id;

    int tid;

    HostId hostId;
    Process* process;
    // If non-null, this address should be cleared and futex-awoken on thread exit.
    // See set_tid_address(2).
    PluginPtr tidAddress;
    int referenceCount;

    SysCallHandler* sys;

    ShMemBlock shimSharedMemBlock;

    // Non-null if blocked by a syscall.
    SysCallCondition* cond;

    // The native, managed thread
    ManagedThread* mthread;

    MAGIC_DECLARE;
};

Thread* thread_new(const Host* host, Process* process, int threadID) {
    Thread* thread = g_new(Thread, 1);
    *thread = (Thread){.referenceCount = 1,
                       .hostId = host_getID(host),
                       .process = process,
                       .tid = threadID,
                       .shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemthread_size()),
                       MAGIC_INITIALIZER};
    process_ref(process);

    thread->sys = syscallhandler_new(host, process, thread);
    thread->mthread = managedthread_new(thread);

    shimshmemthread_init(thread_sharedMem(thread), host_getShimShmemLock(host));

    return thread;
}

static void _thread_cleanupSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    if (thread->cond) {
        syscallcondition_cancel(thread->cond);
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }
}

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_debugAssert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        _thread_cleanupSysCallCondition(thread);
        managedthread_free(thread->mthread);
        if (thread->process) {
            process_unref(thread->process);
            thread->process = NULL;
        }
        if (thread->sys) {
            syscallhandler_unref(thread->sys);
            thread->sys = NULL;
        }
        shmemallocator_globalFree(&thread->shimSharedMemBlock);
        MAGIC_CLEAR(thread);
        g_free(thread);
    }
}

void thread_run(Thread* thread, char* pluginPath, char** argv, char** envv,
                const char* workingDir) {
    MAGIC_ASSERT(thread);
    managedthread_run(thread->mthread, pluginPath, argv, envv, workingDir);
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);

    // Ensure the condition isn't triggered again, but don't clear it yet.
    // Syscall handler can still access.
    if (thread->cond) {
        syscallcondition_cancel(thread->cond);
    }

    SysCallCondition* cond = managedthread_resume(thread->mthread);

    // Now we're done with old condition.
    if (thread->cond) {
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }

    // Wait on new condition.
    thread->cond = cond;
    if (thread->cond) {
        syscallcondition_waitNonblock(
            thread->cond, thread_getHost(thread), thread->process, thread);
    } else {
        utility_debugAssert(!managedthread_isRunning(thread->mthread));
        if (thread->sys) {
            syscallhandler_unref(thread->sys);
            thread->sys = NULL;
        }
    }
}

void thread_handleProcessExit(Thread* thread) {
    MAGIC_ASSERT(thread);
    _thread_cleanupSysCallCondition(thread);
    managedthread_handleProcessExit(thread->mthread);
    /* make sure we cleanup circular refs */
    if (thread->sys) {
        syscallhandler_unref(thread->sys);
        thread->sys = NULL;
    }
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getReturnCode(thread->mthread);
}

bool thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_isRunning(thread->mthread);
}

ShMemBlock* thread_getIPCBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getIPCBlock(thread->mthread);
}

ShMemBlock* thread_getShMBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return &thread->shimSharedMemBlock;
}

ShimShmemThread* thread_sharedMem(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_debugAssert(thread->shimSharedMemBlock.p);
    return thread->shimSharedMemBlock.p;
}

SysCallHandler* thread_getSysCallHandler(Thread* thread) {
    return thread->sys;
}

Process* thread_getProcess(Thread* thread) { return thread->process; }

const Host* thread_getHost(Thread* thread) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == thread->hostId);
    return host;
}

long thread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    va_list(args);
    va_start(args, n);
    long rv = managedthread_nativeSyscall(thread->mthread, n, args);
    va_end(args);
    return rv;
}

int thread_getID(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tid;
}

int thread_clone(Thread* thread, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                 PluginPtr ctid, unsigned long newtls, Thread** child) {
    MAGIC_ASSERT(thread);

    const Host* host = thread_getHost(thread);
    *child = thread_new(host, thread->process, host_getNewProcessID(host));

    int rv = managedthread_clone(
        (*child)->mthread, thread->mthread, flags, child_stack, ptid, ctid, newtls);
    if (rv < 0) {
        thread_unref(*child);
        *child = NULL;
    }
    return rv;
}

uint32_t thread_getProcessId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return process_getProcessID(thread->process);
}

HostId thread_getHostId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->hostId;
}

pid_t thread_getNativePid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getNativePid(thread->mthread);
}

pid_t thread_getNativeTid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getNativeTid(thread->mthread);
}

SysCallCondition* thread_getSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->cond;
}

void thread_clearSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    if (thread->cond) {
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }
}

PluginVirtualPtr thread_getTidAddress(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tidAddress;
}

void thread_setTidAddress(Thread* thread, PluginPtr addr) {
    MAGIC_ASSERT(thread);
    thread->tidAddress = addr;
}

bool thread_isLeader(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tid == process_getProcessID(thread->process);
}

bool thread_unblockedSignalPending(Thread* thread, const ShimShmemHostLock* host_lock) {
    shd_kernel_sigset_t blocked_signals =
        shimshmem_getBlockedSignals(host_lock, thread_sharedMem(thread));
    shd_kernel_sigset_t unblocked_signals = shd_signotset(&blocked_signals);

    {
        shd_kernel_sigset_t thread_pending =
            shimshmem_getThreadPendingSignals(host_lock, thread_sharedMem(thread));
        shd_kernel_sigset_t thread_unblocked_pending =
            shd_sigandset(&thread_pending, &unblocked_signals);
        if (!shd_sigisemptyset(&thread_unblocked_pending)) {
            return true;
        }
    }

    {
        shd_kernel_sigset_t process_pending =
            shimshmem_getProcessPendingSignals(host_lock, process_getSharedMem(thread->process));
        shd_kernel_sigset_t process_unblocked_pending =
            shd_sigandset(&process_pending, &unblocked_signals);
        if (!shd_sigisemptyset(&process_unblocked_pending)) {
            return true;
        }
    }

    return false;
}
