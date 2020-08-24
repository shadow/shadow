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

#include "main/host/syscall_handler.h"
#include "main/host/thread_protected.h"
#include "main/utility/syscall.h"
#include "shim/shim_event.h"
#include "support/logger/logger.h"

Thread thread_create(Host* host, Process* process, int type_id, ThreadMethods methods) {
    Thread thread = {.type_id = type_id,
                     .methods = methods,
                     .referenceCount = 1,
                     .host = host,
                     .process = process,
                     MAGIC_INITIALIZER};
    host_ref(host);
    process_ref(process);
    return thread;
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

void thread_run(Thread* thread, gchar** argv, gchar** envv) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.run);
    thread->nativePid = thread->methods.run(thread, argv, envv);
    // In Linux, the PID is equal to the TID of its first thread.
    thread->nativeTid = thread->nativePid;
}

SysCallCondition* thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.resume);
    return thread->methods.resume(thread);
}

void thread_terminate(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.terminate);
    thread->methods.terminate(thread);
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

const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src,
                                  size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getReadablePtr);
    return thread->methods.getReadablePtr(thread, plugin_src, n);
}

int thread_getReadableString(Thread* thread, PluginPtr plugin_src, size_t n,
                             const char** str, size_t* strlen) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getReadableString);
    return thread->methods.getReadableString(thread, plugin_src, n, str, strlen);
}

void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getReadablePtr);
    return thread->methods.getWriteablePtr(thread, plugin_src, n);
}

void* thread_getMutablePtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.getMutablePtr);
    return thread->methods.getMutablePtr(thread, plugin_src, n);
}

void thread_flushPtrs(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->methods.flushPtrs);
    thread->methods.flushPtrs(thread);
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

PluginPtr thread_mallocPluginPtr(Thread* thread, size_t size) {
    // For now we just implement in terms of thread_nativeSyscall.
    // TODO: We might be able to do something more efficient by delegating to
    // the specific thread implementation, and/or keeping a persistent
    // mmap'd area that we allocate from.
    MAGIC_ASSERT(thread);
    long ptr = thread_nativeSyscall(thread, SYS_mmap, NULL, size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int err = syscall_rawReturnValueToErrno(ptr);
    if (err) {
        error("thread_nativeSyscall(mmap): %s", strerror(err));
        abort();
    }

    // Should be page-aligned.
    utility_assert((ptr % sysconf(_SC_PAGE_SIZE)) == 0);

    return (PluginPtr){.val = ptr};
}

void thread_freePluginPtr(Thread* thread, PluginPtr ptr, size_t size) {
    MAGIC_ASSERT(thread);
    int err =
        syscall_rawReturnValueToErrno(thread_nativeSyscall(thread, SYS_munmap, ptr.val, size));
    if (err) {
        error("thread_nativeSyscall(munmap): %s", strerror(err));
        abort();
    }
}

int thread_getID(Thread *thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getThreadID);
    return thread->getThreadID(thread);
}

Thread *thread_clone(Thread *thread, const SysCallArgs *args) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->clone);
    thread->clone(thread, args);
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
