/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <sys/prctl.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SyscallReturn _syscallhandler_prlimitHelper(SysCallHandler* sys, pid_t pid, int resource,
                                                   UntypedForeignPtr newlim,
                                                   UntypedForeignPtr oldlim) {
    // TODO: for determinism, we may want to enforce static limits for certain resources, like
    // RLIMIT_NOFILE. Some applications like Tor will change behavior depending on these limits.
    if (pid == 0) {
        // process is calling prlimit on itself
        return syscallreturn_makeNative();
    } else {
        // TODO: we do not currently support adjusting other processes limits.
        // To support it, we just need to find the native pid associated
        // with pid, and call prlimit on the native pid instead.
        return syscallreturn_makeDoneErrno(ENOSYS);
    }
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_prctl(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    int option = args->args[0].as_i64;

    switch (option) {
        case PR_CAP_AMBIENT:
        case PR_CAPBSET_READ:
        case PR_CAPBSET_DROP:
        case PR_SET_CHILD_SUBREAPER:
        case PR_GET_CHILD_SUBREAPER:
        case PR_SET_ENDIAN:
        case PR_GET_ENDIAN:
        case PR_SET_FP_MODE:
        case PR_GET_FP_MODE:
        case PR_SET_FPEMU:
        case PR_GET_FPEMU:
        case PR_SET_FPEXC:
        case PR_GET_FPEXC:
        case PR_SET_KEEPCAPS:
        case PR_GET_KEEPCAPS:
        case PR_MCE_KILL:
        case PR_MCE_KILL_GET:
        case PR_MPX_ENABLE_MANAGEMENT:
        case PR_MPX_DISABLE_MANAGEMENT:
        case PR_SET_NAME:
        case PR_GET_NAME:
        case PR_SET_NO_NEW_PRIVS:
        case PR_GET_NO_NEW_PRIVS:
        case PR_SET_MM:
        case PR_SET_PTRACER:
        case PR_SET_SECCOMP:
        case PR_GET_SECCOMP:
        case PR_SET_SECUREBITS:
        case PR_GET_SECUREBITS:
        case PR_GET_SPECULATION_CTRL:
        case PR_SET_THP_DISABLE:
        case PR_TASK_PERF_EVENTS_DISABLE:
        case PR_TASK_PERF_EVENTS_ENABLE:
        case PR_GET_THP_DISABLE:
        case PR_GET_TIMERSLACK:
        case PR_SET_TIMING:
        case PR_GET_TIMING:
        case PR_GET_TSC:
        case PR_GET_UNALIGN:
            trace("prctl %i executing natively", option);
            return syscallreturn_makeNative();
        // Needs emulation to have the desired effect, but also N/A on x86_64.
        case PR_SET_UNALIGN:
        // Executing natively could interfere with shadow's interception of
        // rdtsc. Needs emulation.
        case PR_SET_TSC:
        // Executing natively wouldn't directly hurt anything, but wouldn't
        // have the desired effect.
        case PR_SET_TIMERSLACK:
        // Wouldn't actually hurt correctness, but could significantly hurt
        // performance.
        case PR_SET_SPECULATION_CTRL:
        // We use this signal to ensure managed processes die when Shadow does.
        // Allowing the process to override it could end up allowing orphaned
        // managed processes to live on after shadow exits.
        case PR_SET_PDEATHSIG:
            warning("Not allowing unimplemented prctl %d", option);
            return syscallreturn_makeDoneErrno(ENOSYS);
        case PR_GET_TID_ADDRESS: {
            UntypedForeignPtr tid_addr = thread_getTidAddress(_syscallhandler_getThread(sys));

            // Make sure we have somewhere to copy the output
            UntypedForeignPtr outptr = args->args[1].as_ptr;
            int res = process_writePtr(
                _syscallhandler_getProcess(sys), outptr, &tid_addr.val, sizeof(tid_addr.val));
            if (res) {
                return syscallreturn_makeDoneErrno(-res);
            }
            return syscallreturn_makeDoneU64(0);
        }
        case PR_SET_DUMPABLE: {
            int arg = args->args[1].as_i64;
            switch (arg) {
                case SUID_DUMP_DISABLE:
                case SUID_DUMP_USER:
                    process_setDumpable(_syscallhandler_getProcess(sys), arg);
                    return syscallreturn_makeDoneU64(0);
            };
            return syscallreturn_makeDoneErrno(EINVAL);
        }
        case PR_GET_DUMPABLE:
            return syscallreturn_makeDoneI64(process_getDumpable(_syscallhandler_getProcess(sys)));
    }

    warning("Unknown prctl operation %d", option);
    return syscallreturn_makeDoneErrno(EINVAL);
}

SyscallReturn syscallhandler_prlimit(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int resource = args->args[1].as_i64;
    UntypedForeignPtr newlim = args->args[2].as_ptr; // const struct rlimit*
    UntypedForeignPtr oldlim = args->args[3].as_ptr; // const struct rlimit*
    trace("prlimit called on pid %i for resource %i", pid, resource);
    return _syscallhandler_prlimitHelper(sys, pid, resource, newlim, oldlim);
}

SyscallReturn syscallhandler_prlimit64(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int resource = args->args[1].as_i64;
    UntypedForeignPtr newlim = args->args[2].as_ptr; // const struct rlimit*
    UntypedForeignPtr oldlim = args->args[3].as_ptr; // const struct rlimit*
    trace("prlimit called on pid %i for resource %i", pid, resource);
    return _syscallhandler_prlimitHelper(sys, pid, resource, newlim, oldlim);
}

SyscallReturn syscallhandler_execve(SysCallHandler* sys, const SysCallArgs* args) {
    // The MemoryManager's state is no longer valid after an exec.
    // Destroy it, to be recreated on the next syscall.
    process_resetMemoryManager(_syscallhandler_getProcess(sys));

    // Have the plugin execute it natively.
    return syscallreturn_makeNative();
}
