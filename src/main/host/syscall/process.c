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
