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
    int tid;

    HostId hostId;
    pid_t processId;
    // If non-null, this address should be cleared and futex-awoken on thread exit.
    // See set_tid_address(2).
    PluginPtr tidAddress;

    SysCallHandler* sys;

    ShMemBlock shimSharedMemBlock;

    // Non-null if blocked by a syscall.
    SysCallCondition* cond;

    // The native, managed thread
    ManagedThread* mthread;

    MAGIC_DECLARE;
};

Thread* cthread_new(const Host* host, const ProcessRefCell* process, const ThreadRc* rustThread, int threadID) {
    Thread* thread = g_new(Thread, 1);
    *thread = (Thread){.hostId = host_getID(host),
                       .processId = process_getProcessID(process),
                       .tid = threadID,
                       .shimSharedMemBlock = shmemallocator_globalAlloc(shimshmemthread_size()),
                       MAGIC_INITIALIZER};

    thread->sys = syscallhandler_new(host, process, rustThread);
    thread->mthread = managedthread_new(rustThread);

    shimshmemthread_init(cthread_sharedMem(thread), host_getShimShmemLock(host), threadID);

    return thread;
}

static void _cthread_cleanupSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    if (thread->cond) {
        syscallcondition_cancel(thread->cond);
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }
}

void cthread_free(Thread* thread) {
    MAGIC_ASSERT(thread);
    _cthread_cleanupSysCallCondition(thread);
    managedthread_free(thread->mthread);
    if (thread->sys) {
        syscallhandler_unref(thread->sys);
        thread->sys = NULL;
    }
    shmemallocator_globalFree(&thread->shimSharedMemBlock);
    MAGIC_CLEAR(thread);
    g_free(thread);
}

void cthread_run(Thread* thread, const char* pluginPath, const char* const* argv,
                const char* const* envv_in, const char* workingDir, int straceFd) {
    MAGIC_ASSERT(thread);

    gchar** envv = g_strdupv((gchar**)envv_in);

    // Add shared mem block
    {
        ShMemBlockSerialized sharedMemBlockSerial =
            shmemallocator_globalBlockSerialize(cthread_getShMBlock(thread));

        char sharedMemBlockBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&sharedMemBlockSerial, sharedMemBlockBuf);

        /* append to the env */
        envv = g_environ_setenv(envv, "SHADOW_SHM_THREAD_BLK", sharedMemBlockBuf, TRUE);
    }

    managedthread_run(
        thread->mthread, pluginPath, argv, (const char* const*)envv, workingDir, straceFd);

    g_strfreev(envv);
}

void cthread_resume(Thread* thread) {
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
            thread->cond, cthread_getHost(thread), cthread_getProcess(thread), thread);
    } else {
        utility_debugAssert(!managedthread_isRunning(thread->mthread));
        if (thread->sys) {
            syscallhandler_unref(thread->sys);
            thread->sys = NULL;
        }
    }
}

void cthread_handleProcessExit(Thread* thread) {
    MAGIC_ASSERT(thread);
    _cthread_cleanupSysCallCondition(thread);
    managedthread_handleProcessExit(thread->mthread);
    /* make sure we cleanup circular refs */
    if (thread->sys) {
        syscallhandler_unref(thread->sys);
        thread->sys = NULL;
    }
}
int cthread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getReturnCode(thread->mthread);
}

bool cthread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_isRunning(thread->mthread);
}

ShMemBlock* cthread_getIPCBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getIPCBlock(thread->mthread);
}

ShMemBlock* cthread_getShMBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    return &thread->shimSharedMemBlock;
}

ShimShmemThread* cthread_sharedMem(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_debugAssert(thread->shimSharedMemBlock.p);
    return thread->shimSharedMemBlock.p;
}

SysCallHandler* cthread_getSysCallHandler(Thread* thread) {
    return thread->sys;
}

const ProcessRefCell* cthread_getProcess(Thread* thread) {
    const ProcessRefCell* p = host_getProcess(cthread_getHost(thread), thread->processId);
    utility_alwaysAssert(p);
    return p;
}

const Host* cthread_getHost(Thread* thread) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == thread->hostId);
    return host;
}

long cthread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    va_list(args);
    va_start(args, n);
    long rv = managedthread_nativeSyscall(thread->mthread, n, args);
    va_end(args);
    return rv;
}

int cthread_getID(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tid;
}

int cthread_clone(Thread* thread, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                 PluginPtr ctid, unsigned long newtls, Thread** child) {
    MAGIC_ASSERT(thread);

    const Host* host = cthread_getHost(thread);
    *child = thread_new(host, host_getProcess(host, thread->processId), host_getNewProcessID(host));

    int rv = managedthread_clone(
        (*child)->mthread, thread->mthread, flags, child_stack, ptid, ctid, newtls);
    if (rv < 0) {
        thread_free(*child);
        *child = NULL;
    }
    return rv;
}

uint32_t cthread_getProcessId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->processId;
}

HostId cthread_getHostId(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->hostId;
}

pid_t cthread_getNativePid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getNativePid(thread->mthread);
}

pid_t cthread_getNativeTid(Thread* thread) {
    MAGIC_ASSERT(thread);
    return managedthread_getNativeTid(thread->mthread);
}

SysCallCondition* cthread_getSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->cond;
}

void cthread_clearSysCallCondition(Thread* thread) {
    MAGIC_ASSERT(thread);
    if (thread->cond) {
        syscallcondition_unref(thread->cond);
        thread->cond = NULL;
    }
}

PluginVirtualPtr cthread_getTidAddress(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tidAddress;
}

void cthread_setTidAddress(Thread* thread, PluginPtr addr) {
    MAGIC_ASSERT(thread);
    thread->tidAddress = addr;
}

bool cthread_isLeader(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->tid == thread->processId;
}

bool cthread_unblockedSignalPending(Thread* thread, const ShimShmemHostLock* host_lock) {
    shd_kernel_sigset_t blocked_signals =
        shimshmem_getBlockedSignals(host_lock, cthread_sharedMem(thread));
    shd_kernel_sigset_t unblocked_signals = shd_signotset(&blocked_signals);

    {
        shd_kernel_sigset_t thread_pending =
            shimshmem_getThreadPendingSignals(host_lock, cthread_sharedMem(thread));
        shd_kernel_sigset_t thread_unblocked_pending =
            shd_sigandset(&thread_pending, &unblocked_signals);
        if (!shd_sigisemptyset(&thread_unblocked_pending)) {
            return true;
        }
    }

    {
        shd_kernel_sigset_t process_pending = shimshmem_getProcessPendingSignals(
            host_lock, process_getSharedMem(cthread_getProcess(thread)));
        shd_kernel_sigset_t process_unblocked_pending =
            shd_sigandset(&process_pending, &unblocked_signals);
        if (!shd_sigisemptyset(&process_unblocked_pending)) {
            return true;
        }
    }

    return false;
}
