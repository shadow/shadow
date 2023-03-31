/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "clone.h"

#include <errno.h>
#include <stdlib.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/worker.h"
#include "main/host/syscall/protected.h"
#include "main/utility/utility.h"

SyscallReturn syscallhandler_clone(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    // Note that the syscall args are different than the libc wrapper.
    // See "C library/kernel differences" in clone(2).
    unsigned long flags = args->args[0].as_i64;
    UntypedForeignPtr child_stack = args->args[1].as_ptr;
    UntypedForeignPtr ptid = args->args[2].as_ptr;
    UntypedForeignPtr ctid = args->args[3].as_ptr;
    unsigned long newtls = args->args[4].as_i64;

    unsigned long required_flags =
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM;
    if ((flags & required_flags) != required_flags) {
        warning("Missing a required clone flag in 0x%lx", flags);
        return syscallreturn_makeDoneErrno(ENOTSUP);
    }

    // Don't propagate flags to the real syscall that we'll handle ourselves.
    // Likewise don't pass the original `ptid` and `ctid`, since we should emulate
    // any requested operations on them.
    unsigned long filtered_flags =
        flags & ~(CLONE_PARENT_SETTID | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID);
    pid_t child_tid = thread_clone(_syscallhandler_getThread(sys), filtered_flags, child_stack,
                                   /*ptid*/ (UntypedForeignPtr){.val = 0},
                                   /*ctid*/ (UntypedForeignPtr){.val = 0}, newtls);
    if (child_tid < 0) {
        return syscallreturn_makeDoneI64(child_tid);
    }

    const Thread* child = process_getThread(_syscallhandler_getProcess(sys), child_tid);
    utility_debugAssert(child);

    unsigned long handled_flags = required_flags;
    if (flags & CLONE_PARENT_SETTID) {
        handled_flags |= CLONE_PARENT_SETTID;
        pid_t* ptidp =
            process_getWriteablePtr(_syscallhandler_getProcess(sys), ptid, sizeof(*ptidp));
        *ptidp = thread_getID(child);
    }

    if (flags & CLONE_CHILD_SETTID) {
        handled_flags |= CLONE_CHILD_SETTID;
        pid_t* ctidp =
            process_getWriteablePtr(_syscallhandler_getProcess(sys), ctid, sizeof(*ctidp));
        *ctidp = thread_getID(child);
    }

    if (flags & CLONE_CHILD_CLEARTID) {
        handled_flags |= CLONE_CHILD_CLEARTID;
        thread_setTidAddress(child, ctid);
    }

    // Flags that are OK to have handled in the native clone call
    unsigned long native_handled_flags = CLONE_SETTLS;

    unsigned long unhandled_flags = flags & ~(handled_flags | native_handled_flags);
    if (unhandled_flags) {
        warning("Unhandled clone flags 0x%lx", unhandled_flags);
    }

    return syscallreturn_makeDoneI64(thread_getID(child));
}

SyscallReturn syscallhandler_gettid(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    return syscallreturn_makeDoneI64(sys->threadId);
}
