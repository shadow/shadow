/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_CONDITION_H_
#define SRC_MAIN_HOST_SYSCALL_CONDITION_H_

#include "main/host/descriptor/descriptor_types.h"
#include "main/host/futex.h"
#include "main/host/process.h"
#include "main/host/status.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

/* The type of the object that we use to trigger the condition. */
typedef enum _TriggerType TriggerType;
enum _TriggerType {
    TRIGGER_NONE,
    TRIGGER_DESCRIPTOR,
    TRIGGER_FILE,
    TRIGGER_FUTEX,
};

/* Pointer to the object whose status we monitor for changes */
typedef union _TriggerObject TriggerObject;
union _TriggerObject {
    void* as_pointer;
    LegacyFile* as_legacy_file;
    const File* as_file;
    Futex* as_futex;
};

/* The spec of the condition that will cause us to unblock a process/thread waiting for the object
 * to reach a status. */
typedef struct _Trigger Trigger;
struct _Trigger {
    TriggerType type;
    TriggerObject object;
    Status status;
};

/* Create a new object that will cause a signal to be delivered to
 * a waiting process and thread, conditional upon the given trigger object
 * reaching the given status.
 * The condition starts with a reference count of 1. */
SysCallCondition* syscallcondition_new(Trigger trigger);

/* Add a timeout to the condition. At time `t`, the conditition will be triggered
 * if it hasn't already. `t` is absolute emulated time, as returned by
 * `worker_getCurrentEmulatedTime`. */
void syscallcondition_setTimeout(SysCallCondition* cond, Host* host, CEmulatedTime t);

/* Add a file to the condition which can be used in the syscall handler once it becomes unblocked,
 * without needing to lookup the file again in the descriptor table (since it may no longer exist in
 * the descriptor table). */
void syscallcondition_setActiveFile(SysCallCondition* cond, OpenFile* file);

/* Increment the reference count on the given condition. */
void syscallcondition_ref(SysCallCondition* cond);

/* Decrement the reference count on the given condition, and free the
 * internal state if the reference count reaches 0. */
void syscallcondition_unref(SysCallCondition* cond);

/* Activate the condition by registering the process and thread that will
 * be notified via process_continue() when the condition occurs. After
 * this call, the condition object will begin listening on the status of
 * the timeout and descriptor given in new(). */
void syscallcondition_waitNonblock(SysCallCondition* cond, Host* host, Process* proc,
                                   Thread* thread);

/* Deactivate the condition by deregistering any open listeners and
 * clearing any references to the process an thread given in wait(). */
void syscallcondition_cancel(SysCallCondition* cond);

/* Get the timer for the condition, or EMUTIME_INVALID if there isn't one. */
CEmulatedTime syscallcondition_getTimeout(SysCallCondition* cond);

/* Get the active file for the condition, or NULL if there isn't one. */
OpenFile* syscallcondition_getActiveFile(SysCallCondition* cond);

/* If the condition's thread doesn't have `signo` blocked, schedule a wakeup.
 *
 * Returns whether a wakeup was scheduled.
 */
bool syscallcondition_wakeupForSignal(SysCallCondition* cond, ShimShmemHostLock* hostLock,
                                      int signo);

#endif /* SRC_MAIN_HOST_SYSCALL_CONDITION_H_ */
