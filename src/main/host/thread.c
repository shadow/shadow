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

const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadablePtr);
    return thread->getReadablePtr(thread, plugin_src, n);
}

void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n){
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadablePtr);
    return thread->getWriteablePtr(thread, plugin_src, n);
}
