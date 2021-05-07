/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <sys/prctl.h>

#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SysCallReturn _syscallhandler_prlimitHelper(SysCallHandler* sys, pid_t pid, int resource,
                                                   PluginPtr newlim, PluginPtr oldlim) {
    // TODO: for determinism, we may want to enforce static limits for certain resources, like
    // RLIMIT_NOFILE. Some applications like Tor will change behavior depending on these limits.
    if (pid == 0) {
        // process is calling prlimit on itself
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    } else {
        // TODO: we do not currently support adjusting other processes limits.
        // To support it, we just need to find the native pid associated
        // with pid, and call prlimit on the native pid instead.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_prctl(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    int option = args->args[0].as_i64;
    trace("prctl called with option %i", option);

    if (option == PR_GET_TID_ADDRESS) {
        PluginVirtualPtr tid_addr = thread_getTidAddress(sys->thread);

        // Make sure we have somewhere to copy the output
        PluginPtr outptr = args->args[1].as_ptr;
        if (!outptr.val) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
        }

        int** out = process_getWriteablePtr(sys->process, outptr, sizeof(*out));
        if (!out) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
        }

        *out = (int*)tid_addr.val;
        return (SysCallReturn){.state = SYSCALL_DONE};
    } else {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
}

SysCallReturn syscallhandler_prlimit(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int resource = args->args[1].as_i64;
    PluginPtr newlim = args->args[2].as_ptr; // const struct rlimit*
    PluginPtr oldlim = args->args[3].as_ptr; // const struct rlimit*
    trace("prlimit called on pid %i for resource %i", pid, resource);
    return _syscallhandler_prlimitHelper(sys, pid, resource, newlim, oldlim);
}

SysCallReturn syscallhandler_prlimit64(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int resource = args->args[1].as_i64;
    PluginPtr newlim = args->args[2].as_ptr; // const struct rlimit*
    PluginPtr oldlim = args->args[3].as_ptr; // const struct rlimit*
    trace("prlimit called on pid %i for resource %i", pid, resource);
    return _syscallhandler_prlimitHelper(sys, pid, resource, newlim, oldlim);
}

SysCallReturn syscallhandler_execve(SysCallHandler* sys, const SysCallArgs* args) {
    // The MemoryManager's state is no longer valid after an exec.
    // Destroy it, to be recreated on the next syscall.
    process_setMemoryManager(sys->process, NULL);

    // Have the plugin execute it natively.
    return (SysCallReturn){.state = SYSCALL_NATIVE};
}
