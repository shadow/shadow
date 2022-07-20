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
#include "lib/shim/shim_event.h"
#include "main/core/worker.h"
#include "main/host/affinity.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread_preload.h"
#include "main/host/thread_protected.h"
#include "main/utility/syscall.h"

Thread thread_create(Host* host, Process* process, int threadID, int type_id) {
    Thread thread = {.type_id = type_id,
                     .referenceCount = 1,
                     .host = host,
                     .process = process,
                     .tid = threadID,
                     .affinity = AFFINITY_UNINIT,
                     .shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemthread_size()),
                     MAGIC_INITIALIZER};
    host_ref(host);
    process_ref(process);

    shimshmemthread_init(thread_sharedMem(&thread), &thread);

    // .sys is created (and destroyed) in implementation, since it needs the
    // address of the Thread (which we don't have yet).

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

/*
 * Helper function. Sets the thread's CPU affinity to the worker's affinity.
 */
static void _thread_syncAffinityWithWorker(Thread *thread) {
    thread->affinity =
        affinity_setProcessAffinity(thread->nativeTid, worker_getAffinity(), thread->affinity);
}

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_assert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        _thread_cleanupSysCallCondition(thread);
        threadpreload_free(thread);
        if (thread->process) {
            process_unref(thread->process);
        }
        if (thread->host) {
            host_unref(thread->host);
        }
        shmemallocator_globalFree(&thread->shimSharedMemBlock);
        MAGIC_CLEAR(thread);
        g_free(thread);
    }
}

void thread_run(Thread* thread, char* pluginPath, char** argv, char** envv,
                const char* workingDir) {
    MAGIC_ASSERT(thread);

    _thread_syncAffinityWithWorker(thread);

    thread->nativePid = threadpreload_run(thread, pluginPath, argv, envv, workingDir);
    // In Linux, the PID is equal to the TID of its first thread.
    thread->nativeTid = thread->nativePid;
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    _thread_syncAffinityWithWorker(thread);

    // Ensure the condition isn't triggered again, but don't clear it yet.
    // Syscall handler can still access.
    if (thread->cond) {
        syscallcondition_cancel(thread->cond);
    }

    SysCallCondition* cond = threadpreload_resume(thread);

    // Now we're done with old condition.
    if (thread->cond) {
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }

    // Wait on new condition.
    thread->cond = cond;
    if (thread->cond) {
        syscallcondition_waitNonblock(thread->cond, thread->host, thread->process, thread);
    }
}

void thread_handleProcessExit(Thread* thread) {
    MAGIC_ASSERT(thread);
    _thread_cleanupSysCallCondition(thread);
    threadpreload_handleProcessExit(thread);
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    return threadpreload_getReturnCode(thread);
}

bool thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return threadpreload_isRunning(thread);
}

ShMemBlock* thread_getIPCBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return threadpreload_getIPCBlock(thread);
}

ShMemBlock* thread_getShMBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return &thread->shimSharedMemBlock;
}

ShimShmemThread* thread_sharedMem(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->shimSharedMemBlock.p);
    return thread->shimSharedMemBlock.p;
}

SysCallHandler* thread_getSysCallHandler(Thread* thread) {
    return thread->sys;
}

Process* thread_getProcess(Thread* thread) { return thread->process; }

Host* thread_getHost(Thread* thread) { return thread->host; }

long thread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    va_list(args);
    va_start(args, n);
    long rv = threadpreload_nativeSyscall(thread, n, args);
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
    return threadpreload_clone(thread, flags, child_stack, ptid, ctid, newtls, child);
}

uint32_t thread_getProcessId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return process_getProcessID(thread->process);
}

uint32_t thread_getHostId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return host_getID(thread->host);
}

pid_t thread_getNativePid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->nativePid;
}

pid_t thread_getNativeTid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->nativeTid;
}

SysCallCondition* thread_getSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->cond;
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
