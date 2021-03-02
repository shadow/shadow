/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "clone.h"

#include <errno.h>
#include <stdlib.h>

#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

SysCallReturn syscallhandler_clone(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    // Note that the syscall args are different than the libc wrapper.
    // See "C library/kernel differences" in clone(2).
    unsigned long flags = args->args[0].as_i64;
    PluginPtr child_stack = args->args[1].as_ptr;
    PluginPtr ptid = args->args[2].as_ptr;
    PluginPtr ctid = args->args[3].as_ptr;
    unsigned long newtls = args->args[4].as_i64;

    unsigned long required_flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM;
    if ((flags & required_flags) != required_flags) {
        warning("Missing a required clone flag in 0x%lx", flags);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOTSUP};
    }

    // Don't propagate flags to the real syscall that we'll handle ourselves.
    unsigned long filtered_flags =
        flags & ~(CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID);
    Thread* child = NULL;
    {
        int childpid =
            thread_clone(sys->thread, filtered_flags, child_stack, ptid, ctid, newtls, &child);
        if (childpid < 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = childpid};
        }
    }
    utility_assert(child);

    unsigned long handled_flags = required_flags;
    if (flags & CLONE_PARENT_SETTID) {
        handled_flags |= CLONE_PARENT_SETTID;
        pid_t* ptidp = process_getWriteablePtr(sys->process, sys->thread, ptid, sizeof(*ptidp));
        *ptidp = thread_getID(child);
    }

    if (flags & CLONE_CHILD_SETTID) {
        handled_flags |= CLONE_CHILD_SETTID;
        pid_t* ctidp = process_getWriteablePtr(sys->process, sys->thread, ctid, sizeof(*ctidp));
        *ctidp = thread_getID(child);
    }

    if (flags & CLONE_CHILD_CLEARTID) {
        handled_flags |= CLONE_CHILD_CLEARTID;
        thread_setTidAddress(child, ctid);
    }

    unsigned long unhandled_flags = flags & ~handled_flags;
    if (unhandled_flags) {
        warning("Unhandled clone flags 0x%lx", unhandled_flags);
    }

    // Adds thread to the parent process and schedules it to run. Notably we
    // *don't* want to start running it now, since we're still running the
    // calling thread.
    process_addThread(sys->process, child);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = thread_getID(child)};
}

SysCallReturn syscallhandler_gettid(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = thread_getID(sys->thread)};
}
