/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_condition.h"

#include <stdbool.h>
#include <stdlib.h>

#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/futex.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _SysCallCondition {
    // Specifies how the condition will signal when a status is reached
    Trigger trigger;
    // Non-null if the condition will signal upon a timeout firing
    Timer* timeout;
    // Non-null if we are listening for status updates on a trigger object
    StatusListener* triggerListener;
    // Non-null if we are listening for status updates on the timeout
    StatusListener* timeoutListener;
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

SysCallCondition* syscallcondition_new(Trigger trigger, Timer* timeout) {
    SysCallCondition* cond = malloc(sizeof(*cond));

    *cond = (SysCallCondition){
        .timeout = timeout, .trigger = trigger, .referenceCount = 1, MAGIC_INITIALIZER};

    /* We now hold refs to these objects. */
    if (cond->timeout) {
        descriptor_ref(cond->timeout);
    }

    worker_countObject(OBJECT_TYPE_SYSCALL_CONDITION, COUNTER_TYPE_NEW);

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                descriptor_ref(cond->trigger.object.as_descriptor);
                return cond;
            }
            case TRIGGER_FUTEX: {
                futex_ref(cond->trigger.object.as_futex);
                return cond;
            }
            case TRIGGER_NONE: {
                return cond;
            }
            // No default, forcing a compiler warning if not kept up to date
        }

        // Log error if we get a non-enumerator at run-time.
        error("Unhandled enumerator %d", cond->trigger.type);
    }

    return cond;
}

static void _syscallcondition_cleanupListeners(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    /* Destroy the listeners, which will also unref and free cond. */
    if (cond->timeout && cond->timeoutListener) {
        descriptor_removeListener(
            (LegacyDescriptor*)cond->timeout, cond->timeoutListener);
        statuslistener_setMonitorStatus(cond->timeoutListener, STATUS_NONE, SLF_NEVER);
    }

    if (cond->timeoutListener) {
        statuslistener_unref(cond->timeoutListener);
        cond->timeoutListener = NULL;
    }

    if (cond->trigger.object.as_pointer && cond->triggerListener) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                descriptor_removeListener(
                    cond->trigger.object.as_descriptor, cond->triggerListener);
                break;
            }
            case TRIGGER_FUTEX: {
                futex_removeListener(cond->trigger.object.as_futex, cond->triggerListener);
                break;
            }
            case TRIGGER_NONE: {
                break;
            }
            default: {
                warning("Unhandled enumerator %d", cond->trigger.type);
                break;
            }
        }

        statuslistener_setMonitorStatus(cond->triggerListener, STATUS_NONE, SLF_NEVER);
    }

    if (cond->triggerListener) {
        statuslistener_unref(cond->triggerListener);
        cond->triggerListener = NULL;
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
    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                descriptor_unref(cond->trigger.object.as_descriptor);
                break;
            }
            case TRIGGER_FUTEX: {
                futex_unref(cond->trigger.object.as_futex);
                break;
            }
            case TRIGGER_NONE: {
                break;
            }
            default: {
                warning("Unhandled enumerator %d", cond->trigger.type);
                break;
            }
        }
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

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                g_string_append_printf(string, "status on descriptor %d%s",
                                       descriptor_getHandle(cond->trigger.object.as_descriptor),
                                       cond->timeout ? " and " : "");
                break;
            }
            case TRIGGER_FUTEX: {
                g_string_append_printf(string, "status on futex %p%s",
                                       (void*)futex_getAddress(cond->trigger.object.as_futex).val,
                                       cond->timeout ? " and " : "");
                break;
            }
            case TRIGGER_NONE: {
                break;
            }
            default: {
                warning("Unhandled enumerator %d", cond->trigger.type);
                break;
            }
        }
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

static bool _syscallcondition_statusIsValid(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    switch (cond->trigger.type) {
        case TRIGGER_DESCRIPTOR: {
            if (descriptor_getStatus(cond->trigger.object.as_descriptor) & cond->trigger.status) {
                return true;
            }
            break;
        }
        case TRIGGER_FUTEX: {
            // Futex status doesn't change
            return true;
        }
        case TRIGGER_NONE: {
            break;
        }
        default: {
            warning("Unhandled enumerator %d", cond->trigger.type);
            break;
        }
    }

    return false;
}

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
    if (wasTimeout || _syscallcondition_statusIsValid(cond)) {
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
     * the state of the trigger object again. */
    Task* signalTask =
        task_new(_syscallcondition_signal, cond, (void*)wasTimeout,
                 _syscallcondition_unrefcb, NULL);
    worker_scheduleTask(signalTask, 0); // Call without moving time forward

    syscallcondition_ref(cond);
    task_unref(signalTask);

    cond->signalPending = true;
}

static void _syscallcondition_notifyStatusChanged(void* obj, void* arg) {
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
        cond->timeoutListener = statuslistener_new(
            _syscallcondition_notifyTimeoutExpired, cond, _syscallcondition_unrefcb, NULL, NULL);

        /* The listener holds refs to the thread condition. */
        syscallcondition_ref(cond);

        /* The timer is readable when it expires */
        statuslistener_setMonitorStatus(
            cond->timeoutListener, STATUS_DESCRIPTOR_READABLE, SLF_OFF_TO_ON);

        /* Attach the listener to the timer. */
        descriptor_addListener(
            (LegacyDescriptor*)cond->timeout, cond->timeoutListener);
    }

    if (cond->trigger.object.as_pointer && !cond->triggerListener) {
        /* We listen for status change on the trigger object. */
        cond->triggerListener = statuslistener_new(
            _syscallcondition_notifyStatusChanged, cond, _syscallcondition_unrefcb, NULL, NULL);

        /* The listener holds refs to the thread condition. */
        syscallcondition_ref(cond);

        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                /* Monitor the requested status when it transitions from off to on. */
                statuslistener_setMonitorStatus(
                    cond->triggerListener, cond->trigger.status, SLF_OFF_TO_ON);

                /* Attach the listener to the descriptor. */
                descriptor_addListener(cond->trigger.object.as_descriptor, cond->triggerListener);
                break;
            }
            case TRIGGER_FUTEX: {
                /* Monitor the requested status an every status change. */
                statuslistener_setMonitorStatus(
                    cond->triggerListener, cond->trigger.status, SLF_ALWAYS);

                /* Attach the listener to the descriptor. */
                futex_addListener(cond->trigger.object.as_futex, cond->triggerListener);
                break;
            }
            case TRIGGER_NONE: {
                break;
            }
            default: {
                warning("Unhandled enumerator %d", cond->trigger.type);
                break;
            }
        }
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
