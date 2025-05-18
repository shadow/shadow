/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/syscall.h>

#include "lib/linux-api/linux-api.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/syscall_condition.h"
#include "main/utility/syscall.h"

// Signals for which the shim installs a signal handler. We don't let managed
// code override the handler or change the disposition of these signals.
//
// SIGSYS: Used to catch and handle syscalls via seccomp.
// SIGSEGV: Used to catch and handle usage of rdtsc and rdtscp.
static int _shim_handled_signals[] = {SIGSYS, SIGSEGV};

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_sigaltstack(SyscallHandler* sys, const SyscallArgs* args) {
    utility_debugAssert(sys && args);
    UntypedForeignPtr ss_ptr = args->args[0].as_ptr;
    UntypedForeignPtr old_ss_ptr = args->args[1].as_ptr;
    trace("sigaltstack(%p, %p)", (void*)ss_ptr.val, (void*)old_ss_ptr.val);

    linux_stack_t old_ss =
        shimshmem_getSigAltStack(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                 thread_sharedMem(rustsyscallhandler_getThread(sys)));

    if (ss_ptr.val) {
        if (old_ss.ss_flags & SS_ONSTACK) {
            // sigaltstack(2):  EPERM  An attempt was made to change the
            // alternate signal stack while it was active.
            return syscallreturn_makeDoneErrno(EPERM);
        }

        linux_stack_t new_ss;
        int rv = process_readPtr(rustsyscallhandler_getProcess(sys), &new_ss, ss_ptr, sizeof(new_ss));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
        if (new_ss.ss_flags & SS_DISABLE) {
            // sigaltstack(2): To disable an existing stack, specify ss.ss_flags
            // as SS_DISABLE.  In this case, the kernel ignores any other flags
            // in ss.ss_flags and the remaining fields in ss.
            new_ss = (linux_stack_t){.ss_flags = SS_DISABLE};
        }
        int unrecognized_flags = new_ss.ss_flags & ~(SS_DISABLE | LINUX_SS_AUTODISARM);
        if (unrecognized_flags) {
            debug("Unrecognized signal stack flags %x in %x", unrecognized_flags, new_ss.ss_flags);
            // Unrecognized flag.
            return syscallreturn_makeDoneErrno(EINVAL);
        }
        shimshmem_setSigAltStack(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                 thread_sharedMem(rustsyscallhandler_getThread(sys)), new_ss);
    }

    if (old_ss_ptr.val) {
        int rv =
            process_writePtr(rustsyscallhandler_getProcess(sys), old_ss_ptr, &old_ss, sizeof(old_ss));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    return syscallreturn_makeDoneI64(0);
}
