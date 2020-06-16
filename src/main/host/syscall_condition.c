/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_condition.h"

#include <stdbool.h>
#include <stdlib.h>

#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_listener.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/process.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _SysCallCondition {
    // Non-null if the condition will signal upon a timeout firing
    Timer* timeout;
    // Non-null if the condition will signal when a status is reached
    Descriptor* desc;
    // The status that should cause us to signal
    DescriptorStatus status;
    // Non-null if we are listening for status updates on the timeout
    DescriptorListener* timeoutListener;
    // Non-null if we are listening for status updates on the desc
    DescriptorListener* descListener;
    // The process waiting for the signal
    Process* proc;
    // The thread waiting for the signal
    Thread* thread;
    // If a task to deliver a signal has been scheduled
    bool signalPending;
    // Memory tracking
    gint referenceCount;
    MAGIC_DECLARE;
};

SysCallCondition* syscallcondition_new(Timer* timeout, Descriptor* desc,
                                       DescriptorStatus status) {
    SysCallCondition* cond = malloc(sizeof(*cond));

    *cond = (SysCallCondition){.timeout = timeout,
                               .desc = desc,
                               .status = status,
                               .referenceCount = 1,
                               MAGIC_INITIALIZER};

    /* We now hold refs to these objects. */
    if (timeout) {
        descriptor_ref(timeout);
    }
    if (desc) {
        descriptor_ref(desc);
    }

    worker_countObject(OBJECT_TYPE_SYSCALL_CONDITION, COUNTER_TYPE_NEW);
    return cond;
}

static void _syscallcondition_cleanupListeners(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    /* Destroy the listeners, which will also unref and free cond. */
    if (cond->timeout && cond->timeoutListener) {
        descriptor_removeListener(
            (Descriptor*)cond->timeout, cond->timeoutListener);
        descriptorlistener_setMonitorStatus(
            cond->timeoutListener, DS_NONE, DLF_NEVER);
    }

    if (cond->timeoutListener) {
        descriptorlistener_unref(cond->timeoutListener);
        cond->timeoutListener = NULL;
    }

    if (cond->desc && cond->descListener) {
        descriptor_removeListener(cond->desc, cond->descListener);
        descriptorlistener_setMonitorStatus(
            cond->descListener, DS_NONE, DLF_NEVER);
    }

    if (cond->descListener) {
        descriptorlistener_unref(cond->descListener);
        cond->descListener = NULL;
    }
}

static void _syscallcondition_cleanupProc(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    if (cond->proc) {
        process_unref(cond->proc);
        cond->proc = NULL;
    }

    if (cond->thread) {
        thread_unref(cond->thread);
        cond->thread = NULL;
    }
}

static void _syscallcondition_free(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    _syscallcondition_cleanupListeners(cond);
    _syscallcondition_cleanupProc(cond);

    if (cond->timeout) {
        descriptor_unref(cond->timeout);
    }
    if (cond->desc) {
        descriptor_unref(cond->desc);
    }

    MAGIC_CLEAR(cond);
    free(cond);
    worker_countObject(OBJECT_TYPE_SYSCALL_CONDITION, COUNTER_TYPE_FREE);
}

void syscallcondition_ref(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);
    cond->referenceCount++;
}

void syscallcondition_unref(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);
    cond->referenceCount--;
    utility_assert(cond->referenceCount >= 0);
    if (cond->referenceCount == 0) {
        _syscallcondition_free(cond);
    }
}

static void _syscallcondition_unrefcb(void* cond_ptr) {
    syscallcondition_unref(cond_ptr);
}

#ifdef DEBUG
static void _syscallcondition_logListeningState(SysCallCondition* cond,
                                                const char* listenVerb) {
    GString* string = g_string_new(NULL);

    g_string_append_printf(string, "Process %s thread %p %s listening for ",
                           process_getName(cond->proc), cond->thread,
                           listenVerb);

    if (cond->desc) {
        g_string_append_printf(string, "status on descriptor %d%s",
                               descriptor_getHandle(cond->desc),
                               cond->timeout ? " and " : "");
    }
    if (cond->timeout) {
        struct itimerspec value = {0};
        utility_assert(timer_getTime(cond->timeout, &value) == 0);
        g_string_append_printf(string, "a timeout of %lu.%09lu seconds",
                               (unsigned long)value.it_value.tv_sec,
                               (unsigned long)value.it_value.tv_nsec);
    }

    debug("%s", string->str);

    g_string_free(string, TRUE);
}
#endif

static void _syscallcondition_signal(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    bool wasTimeout = (bool)arg;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "signaling while");
#endif

    cond->signalPending = false;

    // Always deliver the signal if the timeout expired.
    // Otherwise, only deliver the signal if the desc status is still valid.
    if (wasTimeout || (descriptor_getStatus(cond->desc) & cond->status)) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, "stopped");
#endif

        /* Deliver the signal to notify the process to continue. */
        process_continue(cond->proc, cond->thread);
    }
}

static void _syscallcondition_scheduleSignalTask(SysCallCondition* cond,
                                                 bool wasTimeout) {
    MAGIC_ASSERT(cond);

    /* We deliver the signal via a task, to make sure whatever
     * code triggered our listener finishes its logic first before
     * we tell the process to run the plugin and potentially change
     * the state of the descriptor again. */
    Task* signalTask =
        task_new(_syscallcondition_signal, cond, (void*)wasTimeout,
                 _syscallcondition_unrefcb, NULL);
    worker_scheduleTask(signalTask, 0); // Call without moving time forward

    syscallcondition_ref(cond);
    task_unref(signalTask);

    cond->signalPending = true;
}

static void _syscallcondition_notifyDescStatusChanged(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "status changed while");
#endif

    // Deliver one signal even if desc status changes many times.
    if (!cond->signalPending) {
        _syscallcondition_scheduleSignalTask(cond, false);
    }
}

static void _syscallcondition_notifyTimeoutExpired(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "timeout expired while");
#endif

    // Deliver one signal even if timeout status changes many times.
    if (!cond->signalPending) {
        _syscallcondition_scheduleSignalTask(cond, true);
    }
}

void syscallcondition_waitNonblock(SysCallCondition* cond, Process* proc,
                                   Thread* thread) {
    MAGIC_ASSERT(cond);
    utility_assert(proc);
    utility_assert(thread);

    /* Update the reference counts. */
    syscallcondition_cancel(cond);
    cond->proc = proc;
    process_ref(proc);
    cond->thread = thread;
    thread_ref(thread);

    /* Now set up the listeners. */
    if (cond->timeout && !cond->timeoutListener) {
        /* The timer is used for timeouts. */
        cond->timeoutListener = descriptorlistener_new(
            _syscallcondition_notifyTimeoutExpired, cond,
            _syscallcondition_unrefcb, NULL, NULL);

        /* The listener holds refs to the thread condition. */
        syscallcondition_ref(cond);

        /* The timer is readable when it expires */
        descriptorlistener_setMonitorStatus(
            cond->timeoutListener, DS_READABLE, DLF_OFF_TO_ON);

        /* Attach the listener to the timer. */
        descriptor_addListener(
            (Descriptor*)cond->timeout, cond->timeoutListener);
    }

    if (cond->desc && !cond->descListener) {
        /* We listen for status change on the descriptor. */
        cond->descListener = descriptorlistener_new(
            _syscallcondition_notifyDescStatusChanged, cond,
            _syscallcondition_unrefcb, NULL, NULL);

        /* The listener holds refs to the thread condition. */
        syscallcondition_ref(cond);

        /* Monitor the requested status. */
        descriptorlistener_setMonitorStatus(
            cond->descListener, cond->status, DLF_OFF_TO_ON);

        /* Attach the listener to the descriptor. */
        descriptor_addListener(cond->desc, cond->descListener);
    }

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "started");
#endif
}

void syscallcondition_cancel(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);
    _syscallcondition_cleanupListeners(cond);
    _syscallcondition_cleanupProc(cond);
}
