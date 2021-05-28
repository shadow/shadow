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

#include "main/core/worker.h"
#include "main/host/affinity.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread_protected.h"
#include "main/utility/syscall.h"
#include "shim/shim_event.h"
#include "support/logger/logger.h"

Thread thread_create(Host* host, Process* process, int threadID, int type_id,
                     ThreadMethods methods) {
    Thread thread = {.type_id = type_id,
                     .methods = methods,
                     .referenceCount = 1,
                     .host = host,
                     .process = process,
                     .tid = threadID,
                     .affinity = AFFINITY_UNINIT,
                     MAGIC_INITIALIZER};
    host_ref(host);
    process_ref(process);

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
        thread->methods.free(thread);
        if (thread->process) {
            process_unref(thread->process);
        }
        if (thread->host) {
            host_unref(thread->host);
        }
        MAGIC_CLEAR(thread);
        g_free(thread);
    }
}

void thread_run(Thread* thread, gchar** argv, gchar** envv, const char* workingDir) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.run);

    _thread_syncAffinityWithWorker(thread);

    thread->nativePid = thread->methods.run(thread, argv, envv, workingDir);
    // In Linux, the PID is equal to the TID of its first thread.
    thread->nativeTid = thread->nativePid;
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.resume);
    _thread_syncAffinityWithWorker(thread);
    _thread_cleanupSysCallCondition(thread);
    thread->cond = thread->methods.resume(thread);
    if (thread->cond) {
        syscallcondition_waitNonblock(thread->cond, thread->process, thread);
    }
}

void thread_handleProcessExit(Thread* thread) {
    MAGIC_ASSERT(thread);
    _thread_cleanupSysCallCondition(thread);
    utility_assert(thread->methods.handleProcessExit);
    thread->methods.handleProcessExit(thread);
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getReturnCode);
    return thread->methods.getReturnCode(thread);
}

bool thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->methods.isRunning(thread);
}

ShMemBlock* thread_getIPCBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getIPCBlock);
    return thread->methods.getIPCBlock(thread);
}

ShMemBlock* thread_getShMBlock(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getShMBlock);
    return thread->methods.getShMBlock(thread);
}

SysCallHandler* thread_getSysCallHandler(Thread* thread) {
    return thread->sys;
}

long thread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.nativeSyscall);
    va_list(args);
    va_start(args, n);
    long rv = thread->methods.nativeSyscall(thread, n, args);
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
    utility_assert(thread->methods.clone);
    return thread->methods.clone(thread, flags, child_stack, ptid, ctid, newtls, child);
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