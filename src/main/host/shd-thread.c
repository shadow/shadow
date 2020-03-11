/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/shd-thread.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shim/shim_event.h"
#include "main/host/shd-syscall-handler.h"
#include "main/host/shd-thread-protected.h"
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
    thread->run(thread, argv, envv);
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    thread->resume(thread);
}

void thread_setSysCallResult(Thread* thread, SysCallReg retval) {
    MAGIC_ASSERT(thread);
    thread->setSysCallResult(thread, retval);
}

void thread_terminate(Thread* thread) {
    MAGIC_ASSERT(thread);
    thread->terminate(thread);
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->getReturnCode(thread);
}

gboolean thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->isRunning(thread);
}
