/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/thread.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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

void thread_run(Thread* thread, gchar** argv, gchar** envv, int stderrFD,
                int stdoutFD) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->run);
    thread->run(thread, argv, envv, stderrFD, stdoutFD);
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

long thread_syscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->syscall);
    va_list(args);
    va_start(args, n);
    long rv = thread->syscall(thread, n, args);
    va_end(args);
    return rv;
}
