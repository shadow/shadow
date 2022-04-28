/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_condition.h"

#include <stdbool.h>
#include <stdlib.h>
#include <sys/timerfd.h>

#include "lib/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/futex.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"

struct _SysCallCondition {
    // A trigger to unblock the syscall.
    Trigger trigger;
    // Non-null if the condition will trigger upon a timeout firing.
    TimerFd* timeout;
    // The active file in the blocked syscall. This is state used when resuming a blocked syscall.
    OpenFile* activeFile;
    // Non-null if we are listening for status updates on a trigger object
    StatusListener* triggerListener;
    // Non-null if we are listening for status updates on the timeout
    StatusListener* timeoutListener;
    // The process waiting for the condition
    Process* proc;
    // The thread waiting for the condition
    Thread* thread;
    // Whether a wakeup event has already been scheduled.
    // Used to avoid scheduling multiple events when multiple triggers fire.
    bool wakeupScheduled;
    // Memory tracking
    gint referenceCount;
    MAGIC_DECLARE;
};

SysCallCondition* syscallcondition_new(Trigger trigger) {
    SysCallCondition* cond = malloc(sizeof(*cond));

    *cond = (SysCallCondition){
        .timeout = NULL, .trigger = trigger, .referenceCount = 1, MAGIC_INITIALIZER};

    /* We now hold refs to these objects. */
    if (cond->timeout) {
        descriptor_ref(cond->timeout);
    }

    worker_count_allocation(SysCallCondition);

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                descriptor_ref(cond->trigger.object.as_descriptor);
                return cond;
            }
            case TRIGGER_FILE: {
                /* The file represents an Arc, so the reference count is fine;
                 * Just need to remember to drop it later.
                 */
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

        // Log panic if we get a non-enumerator at run-time.
        utility_panic("Unhandled enumerator %d", cond->trigger.type);
    }

    return cond;
}

void syscallcondition_setTimeout(SysCallCondition* cond, Host* host, EmulatedTime t) {
    MAGIC_ASSERT(cond);

    if (!cond->timeout) {
        cond->timeout = timerfd_new();
    }

    struct itimerspec itimerspec = {0};
    itimerspec.it_value.tv_sec = t / SIMTIME_ONE_SECOND;
    t -= itimerspec.it_value.tv_sec * SIMTIME_ONE_SECOND;
    itimerspec.it_value.tv_nsec = t / SIMTIME_ONE_NANOSECOND;

    int rv = timerfd_setTime(cond->timeout, host, TFD_TIMER_ABSTIME, &itimerspec, NULL);
    if (rv != 0) {
        panic("timerfd_setTime: %s", strerror(-rv));
    }
}

void syscallcondition_setActiveFile(SysCallCondition* cond, OpenFile* file) {
    MAGIC_ASSERT(cond);

    if (cond->activeFile) {
        openfile_drop(cond->activeFile);
    }

    cond->activeFile = file;
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
            case TRIGGER_FILE: {
                file_removeListener(cond->trigger.object.as_file, cond->triggerListener);
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

    if (cond->activeFile) {
        openfile_drop(cond->activeFile);
        cond->activeFile = NULL;
    }

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                descriptor_unref(cond->trigger.object.as_descriptor);
                break;
            }
            case TRIGGER_FILE: {
                file_drop(cond->trigger.object.as_file);
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
    worker_count_deallocation(SysCallCondition);
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
                           cond->proc ? process_getName(cond->proc) : "NULL", cond->thread,
                           listenVerb);

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                g_string_append_printf(string, "status on descriptor %d%s",
                                       descriptor_getHandle(cond->trigger.object.as_descriptor),
                                       cond->timeout ? " and " : "");
                break;
            }
            case TRIGGER_FILE: {
                g_string_append_printf(string, "status on file %p%s",
                                       (void*)cond->trigger.object.as_file,
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
        timerfd_getTime(cond->timeout, &value);
        g_string_append_printf(string, "a timeout of %lu.%09lu seconds",
                               (unsigned long)value.it_value.tv_sec,
                               (unsigned long)value.it_value.tv_nsec);
    }

    trace("%s", string->str);

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
        case TRIGGER_FILE: {
            if (file_getStatus(cond->trigger.object.as_file) & cond->trigger.status) {
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

static bool _syscallcondition_satisfied(SysCallCondition* cond, Host* host) {
    TimerFd* timeout = syscallcondition_getTimeout(cond);
    if (timeout && timerfd_getExpirationCount(timeout) > 0) {
        // Unclear what the semantics would be here if a repeating timer were
        // used.
        utility_assert(timerfd_getExpirationCount(timeout) == 1);

        // Timed out.
        return true;
    }
    if (_syscallcondition_statusIsValid(cond)) {
        // Primary condition is satisfied.
        return true;
    }
    bool signalPending = thread_unblockedSignalPending(cond->thread, host_getShimShmemLock(host));
    if (signalPending) {
        return true;
    }
    return false;
}

static void _syscallcondition_trigger(Host* host, void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

    // The wakeup is executing here and now. Setting to false allows
    // the callback to be scheduled again if the condition isn't canceled
    // (which it will be, if we decide to actually run the process below).
    cond->wakeupScheduled = false;

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "wakeup while");
#endif

    if (!cond->proc || !cond->thread) {
        utility_assert(!cond->proc);
        utility_assert(!cond->thread);
        utility_assert(!cond->timeoutListener);
        utility_assert(!cond->triggerListener);
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, "ignored (already cleaned up)");
#endif
        return;
    }

    // Always deliver the wakeup if the timeout expired.
    // Otherwise, only deliver the wakeup if the desc status is still valid.
    if (_syscallcondition_satisfied(cond, host)) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, "stopped");
#endif

        /* Wake up the thread. */
        process_continue(cond->proc, cond->thread);
    } else {
        // Spurious wakeup. Just return without running the process. The
        // condition's listeners should still be installed, and now that we've
        // flipped `wakeupScheduled`, they can schedule this wakeup again.
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, "re-blocking");
#endif
    }
}

static void _syscallcondition_scheduleWakeupTask(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    if (cond->wakeupScheduled) {
        // Deliver one wakeup even if condition is triggered multiple times or
        // ways.
        return;
    }

    /* We deliver the wakeup via a task, to make sure whatever
     * code triggered our listener finishes its logic first before
     * we tell the process to run the plugin and potentially change
     * the state of the trigger object again. */
    Task* wakeupTask =
        task_new(_syscallcondition_trigger, cond, NULL, _syscallcondition_unrefcb, NULL);
    worker_scheduleTaskWithDelay(
        wakeupTask, thread_getHost(cond->thread), 0); // Call without moving time forward

    syscallcondition_ref(cond);
    task_unref(wakeupTask);

    cond->wakeupScheduled = true;
}

static void _syscallcondition_notifyStatusChanged(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "status changed while");
#endif

    _syscallcondition_scheduleWakeupTask(cond);
}

static void _syscallcondition_notifyTimeoutExpired(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "timeout expired while");
#endif

    _syscallcondition_scheduleWakeupTask(cond);
}

void syscallcondition_waitNonblock(SysCallCondition* cond, Process* proc, Thread* thread,
                                   Host* host) {
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
        cond->timeoutListener = statuslistener_new(_syscallcondition_notifyTimeoutExpired, cond,
                                                   _syscallcondition_unrefcb, NULL, NULL, host);

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
        cond->triggerListener = statuslistener_new(_syscallcondition_notifyStatusChanged, cond,
                                                   _syscallcondition_unrefcb, NULL, NULL, host);

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
            case TRIGGER_FILE: {
                /* Monitor the requested status when it transitions from off to on. */
                statuslistener_setMonitorStatus(
                    cond->triggerListener, cond->trigger.status, SLF_OFF_TO_ON);

                /* Attach the listener to the descriptor. */
                file_addListener(cond->trigger.object.as_file, cond->triggerListener);
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

bool syscallcondition_wakeupForSignal(SysCallCondition* cond, ShimShmemHostLock* hostLock,
                                      int signo) {
    MAGIC_ASSERT(cond);

    shd_kernel_sigset_t blockedSignals =
        shimshmem_getBlockedSignals(hostLock, thread_sharedMem(cond->thread));
    if (shd_sigismember(&blockedSignals, signo)) {
        // Signal is blocked. Don't schedule.
        return false;
    }

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, "signaled while");
#endif

    _syscallcondition_scheduleWakeupTask(cond);
    return true;
}

TimerFd* syscallcondition_getTimeout(SysCallCondition* cond) { return cond->timeout; }

OpenFile* syscallcondition_getActiveFile(SysCallCondition* cond) { return cond->activeFile; }
