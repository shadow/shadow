/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_condition.h"

#include <stdbool.h>
#include <stdlib.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/futex.h"
#include "main/host/process.h"
#include "main/host/status_listener.h"
#include "main/utility/utility.h"

struct _SysCallCondition {
    // A trigger to unblock the syscall.
    Trigger trigger;
    // Time at which the condition will expire, or EMUTIME_INVALID if no timeout.
    CEmulatedTime timeoutExpiration;
    // Timeout object waiting for timeoutExpiration.
    Timer* timeout;
    // The active file in the blocked syscall. This is state used when resuming a blocked syscall.
    OpenFile* activeFile;
    // Non-null if we are listening for status updates on a trigger object
    StatusListener* triggerListener;
    // The host
    HostId hostId;
    // The process waiting for the condition
    pid_t proc;
    // The thread waiting for the condition
    pid_t threadId;
    // Whether a wakeup event has already been scheduled.
    // Used to avoid scheduling multiple events when multiple triggers fire.
    bool wakeupScheduled;
    // Memory tracking
    gint referenceCount;
    MAGIC_DECLARE;
};

static void _syscallcondition_unrefcb(void* cond_ptr);
static void _syscallcondition_notifyTimeoutExpired(const Host* host, void* obj, void* arg);

SysCallCondition* syscallcondition_new(Trigger trigger) {
    SysCallCondition* cond = malloc(sizeof(*cond));

    *cond = (SysCallCondition){.timeoutExpiration = EMUTIME_INVALID,
                               .timeout = NULL,
                               .trigger = trigger,
                               .referenceCount = 1,
                               MAGIC_INITIALIZER};

    worker_count_allocation(SysCallCondition);

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                legacyfile_ref(cond->trigger.object.as_legacy_file);
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
            case TRIGGER_CHILD: {
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

SysCallCondition* syscallcondition_newWithAbsTimeout(CEmulatedTime abs_trigger_time) {
    utility_debugAssert(abs_trigger_time >= worker_getCurrentEmulatedTime());
    SysCallCondition* cond = syscallcondition_new((Trigger){.type = TRIGGER_NONE});
    syscallcondition_setTimeout(cond, abs_trigger_time);
    return cond;
}

void syscallcondition_setTimeout(SysCallCondition* cond, CEmulatedTime t) {
    MAGIC_ASSERT(cond);

    cond->timeoutExpiration = t;
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

    if (cond->timeout) {
        timer_disarm(cond->timeout);
        timer_drop(cond->timeout);
        cond->timeout = NULL;
    }

    if (cond->trigger.object.as_pointer && cond->triggerListener) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                legacyfile_removeListener(
                    cond->trigger.object.as_legacy_file, cond->triggerListener);
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
            case TRIGGER_CHILD: {
                const Host* host = worker_getCurrentHost();
                const Process* proc = worker_getCurrentProcess();
                process_removeChildEventListener(host, proc, cond->triggerListener);
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

static void _syscallcondition_free(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    _syscallcondition_cleanupListeners(cond);

    if (cond->timeout) {
        timer_disarm(cond->timeout);
        timer_drop(cond->timeout);
        cond->timeout = NULL;
    }

    if (cond->activeFile) {
        openfile_drop(cond->activeFile);
        cond->activeFile = NULL;
    }

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                legacyfile_unref(cond->trigger.object.as_legacy_file);
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
            case TRIGGER_CHILD: {
                // Not a pointer; nothing to unref.
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
    utility_debugAssert(cond->referenceCount >= 0);
    if (cond->referenceCount == 0) {
        _syscallcondition_free(cond);
    }
}

static void _syscallcondition_unrefcb(void* cond_ptr) {
    syscallcondition_unref(cond_ptr);
}

#ifdef DEBUG
static void _syscallcondition_logListeningState(SysCallCondition* cond, const Process* proc,
                                                const char* listenVerb) {
    GString* string = g_string_new(NULL);

    g_string_append_printf(string, "Process %s thread %d %s listening for ",
                           proc ? process_getName(proc) : "NULL", cond->threadId, listenVerb);

    if (cond->trigger.object.as_pointer) {
        switch (cond->trigger.type) {
            case TRIGGER_DESCRIPTOR: {
                g_string_append_printf(string, "status on descriptor %p%s",
                                       cond->trigger.object.as_legacy_file,
                                       cond->timeoutExpiration != EMUTIME_INVALID ? " and " : "");
                break;
            }
            case TRIGGER_FILE: {
                g_string_append_printf(string, "status on file %p%s",
                                       (void*)cond->trigger.object.as_file,
                                       cond->timeoutExpiration != EMUTIME_INVALID ? " and " : "");
                break;
            }
            case TRIGGER_FUTEX: {
                g_string_append_printf(string, "status on futex %p%s",
                                       (void*)futex_getAddress(cond->trigger.object.as_futex).val,
                                       cond->timeoutExpiration != EMUTIME_INVALID ? " and " : "");
                break;
            }
            case TRIGGER_CHILD: {
                g_string_append_printf(string, "status on child-process-listener %s",
                            cond->timeoutExpiration != EMUTIME_INVALID ? " and " : "");
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

    if (cond->timeoutExpiration != EMUTIME_INVALID) {
        utility_debugAssert(cond->timeoutExpiration >= worker_getCurrentEmulatedTime());
        CSimulationTime remainingTime = cond->timeoutExpiration - worker_getCurrentEmulatedTime();
        g_string_append_printf(string, "a timeout with %llu.%09llu seconds remaining",
                               remainingTime / SIMTIME_ONE_SECOND,
                               (remainingTime % SIMTIME_ONE_SECOND) / SIMTIME_ONE_NANOSECOND);
    }

    trace("%s", string->str);

    g_string_free(string, TRUE);
}
#endif

static bool _syscallcondition_statusIsValid(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);

    switch (cond->trigger.type) {
        case TRIGGER_DESCRIPTOR: {
            if (legacyfile_getStatus(cond->trigger.object.as_legacy_file) & cond->trigger.status) {
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
        case TRIGGER_CHILD: {
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

static bool _syscallcondition_satisfied(SysCallCondition* cond, const Host* host, const Thread* thread) {
    if (cond->timeoutExpiration != EMUTIME_INVALID &&
        worker_getCurrentEmulatedTime() >= cond->timeoutExpiration) {
        // Timed out.
        return true;
    }
    if (_syscallcondition_statusIsValid(cond)) {
        // Primary condition is satisfied.
        return true;
    }
    bool signalPending = thread_unblockedSignalPending(thread, host_getShimShmemLock(host));
    if (signalPending) {
        return true;
    }
    return false;
}

static void _syscallcondition_trigger(const Host* host, void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

    // The wakeup is executing here and now. Setting to false allows
    // the callback to be scheduled again if the condition isn't canceled
    // (which it will be, if we decide to actually run the process below).
    cond->wakeupScheduled = false;

    const Process* proc = host_getProcess(host, cond->proc);
    if (!proc) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, proc, "ignored (process no longer exists)");
#endif
        return;
    }

    if (!process_isRunning(proc)) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, proc, "ignored (process no longer running)");
#endif
        return;
    }

    const Thread* thread = process_getThread(proc, cond->threadId);
    if (!thread) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, proc, "ignored (thread no longer exists)");
#endif
        return;
    }

#ifdef DEBUG
    _syscallcondition_logListeningState(cond, proc, "wakeup while");
#endif

    // Always deliver the wakeup if the timeout expired.
    // Otherwise, only deliver the wakeup if the desc status is still valid.
    if (_syscallcondition_satisfied(cond, host, thread)) {
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, proc, "stopped");
#endif

        /* Wake up the thread. */
        host_continue(host, cond->proc, cond->threadId);
    } else {
        // Spurious wakeup. Just return without running the process. The
        // condition's listeners should still be installed, and now that we've
        // flipped `wakeupScheduled`, they can schedule this wakeup again.
#ifdef DEBUG
        _syscallcondition_logListeningState(cond, proc, "re-blocking");
#endif
    }
}

static void _syscallcondition_scheduleWakeupTask(SysCallCondition* cond, const Host* host) {
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
    TaskRef* wakeupTask = taskref_new_bound(
        cond->hostId, _syscallcondition_trigger, cond, NULL, _syscallcondition_unrefcb, NULL);
    host_scheduleTaskWithDelay(host, wakeupTask, 0); // Call without moving time forward

    syscallcondition_ref(cond);
    taskref_drop(wakeupTask);

    cond->wakeupScheduled = true;
}

static void _syscallcondition_notifyStatusChanged(void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

    const Host* host = worker_getCurrentHost();

#ifdef DEBUG
    const Process* proc = host_getProcess(host, cond->proc);
    _syscallcondition_logListeningState(cond, proc, "status changed while");
#endif

    _syscallcondition_scheduleWakeupTask(cond, host);
}

static void _syscallcondition_notifyTimeoutExpired(const Host* host, void* obj, void* arg) {
    SysCallCondition* cond = obj;
    MAGIC_ASSERT(cond);

#ifdef DEBUG
    const Process* proc = host_getProcess(host, cond->proc);
    _syscallcondition_logListeningState(cond, proc, "timeout expired while");
#endif

    _syscallcondition_scheduleWakeupTask(cond, host);
}

void syscallcondition_waitNonblock(SysCallCondition* cond, const Host* host, const Process* proc,
                                   const Thread* thread) {
    MAGIC_ASSERT(cond);
    utility_debugAssert(host);
    utility_debugAssert(proc);
    utility_debugAssert(thread);

    /* Update the reference counts. */
    syscallcondition_cancel(cond);
    cond->hostId = host_getID(host);
    cond->proc = process_getProcessID(proc);
    cond->threadId = thread_getID(thread);

    if (cond->timeoutExpiration != EMUTIME_INVALID) {
        if (!cond->timeout) {
            syscallcondition_ref(cond);
            TaskRef* task = taskref_new_bound(cond->hostId, _syscallcondition_notifyTimeoutExpired,
                                              cond, NULL, _syscallcondition_unrefcb, NULL);
            cond->timeout = timer_new(task);
            taskref_drop(task);
        }

        timer_arm(cond->timeout, host, cond->timeoutExpiration, 0);
    }

    /* Now set up the listeners. */
    if (!cond->triggerListener) {
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
                legacyfile_addListener(cond->trigger.object.as_legacy_file, cond->triggerListener);
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
            case TRIGGER_CHILD: {
                /* Monitor the requested status an every status change. */
                statuslistener_setMonitorStatus(
                    cond->triggerListener, cond->trigger.status, SLF_ALWAYS);

                /* Attach the listener to current process. */
                process_addChildEventListener(host, proc, cond->triggerListener);

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
    _syscallcondition_logListeningState(cond, proc, "started");
#endif
}

void syscallcondition_cancel(SysCallCondition* cond) {
    MAGIC_ASSERT(cond);
    _syscallcondition_cleanupListeners(cond);
}

bool syscallcondition_wakeupForSignal(SysCallCondition* cond, const Host* host, int signo) {
    MAGIC_ASSERT(cond);

    ShimShmemHostLock* hostLock = host_getShimShmemLock(host);
    const Thread* thread = host_getThread(host, cond->threadId);
    linux_sigset_t blockedSignals = shimshmem_getBlockedSignals(hostLock, thread_sharedMem(thread));
    if (linux_sigismember(&blockedSignals, signo)) {
        // Signal is blocked. Don't schedule.
        return false;
    }

#ifdef DEBUG
    const Process* proc = host_getProcess(host, cond->proc);
    _syscallcondition_logListeningState(cond, proc, "signaled while");
#endif

    _syscallcondition_scheduleWakeupTask(cond, host);
    return true;
}

CEmulatedTime syscallcondition_getTimeout(SysCallCondition* cond) {
    return cond->timeoutExpiration;
}

OpenFile* syscallcondition_getActiveFile(SysCallCondition* cond) { return cond->activeFile; }
