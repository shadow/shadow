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
#include "shim/shim_event.h"
#include "support/logger/logger.h"

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_assert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        thread->free(thread);
    }
}

void thread_run(Thread* thread, gchar** argv, gchar** envv) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->run);
    thread->run(thread, argv, envv);
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->resume);
    thread->resume(thread);
}

void thread_terminate(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->terminate);
    thread->terminate(thread);
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReturnCode);
    return thread->getReturnCode(thread);
}

gboolean thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->isRunning(thread);
}

void* thread_newClonedPtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->newClonedPtr);
    return thread->newClonedPtr(thread, plugin_src, n);
}

void thread_releaseClonedPtr(Thread* thread, void* p) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->releaseClonedPtr);
    thread->releaseClonedPtr(thread, p);
}

const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src,
                                  size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadablePtr);
    return thread->getReadablePtr(thread, plugin_src, n);
}

int thread_getReadableString(Thread* thread, PluginPtr plugin_src, size_t n,
                             const char** str, size_t* strlen) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadableString);
    return thread->getReadableString(thread, plugin_src, n, str, strlen);
}

void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadablePtr);
    return thread->getWriteablePtr(thread, plugin_src, n);
}

void thread_flushPtrs(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->flushPtrs);
    thread->flushPtrs(thread);
}

long thread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->nativeSyscall);
    va_list(args);
    va_start(args, n);
    long rv = thread->nativeSyscall(thread, n, args);
    va_end(args);
    return rv;
}

// TODO put this into a common utility library.
static long _errnoForSyscallRetval(long rv) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (rv <= -1 && rv >= -4095) {
        return -rv;
    }
    return 0;
}

PluginPtr thread_mallocPluginPtr(Thread* thread, size_t size) {
    // For now we just implement in terms of thread_nativeSyscall.
    // TODO: We might be able to do something more efficient by delegating to
    // the specific thread implementation.
    MAGIC_ASSERT(thread);
    long ptr = thread_nativeSyscall(thread, SYS_mmap, NULL, size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    long err = _errnoForSyscallRetval(ptr);
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
    long err = _errnoForSyscallRetval(
        thread_nativeSyscall(thread, SYS_munmap, ptr.val, size));
    if (err) {
        error("thread_nativeSyscall(munmap): %s", strerror(err));
        abort();
    }
}
