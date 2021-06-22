/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/syscall.h>

#include "main/host/host.h"
#include "main/host/shimipc.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "main/utility/syscall.h"
#include "lib/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SysCallReturn _syscallhandler_killHelper(SysCallHandler* sys, pid_t pid, pid_t tid, int sig,
                                                long syscallnum) {
    pid_t my_tid = thread_getNativeTid(sys->thread);

    // Return error if trying to stop/continue a process so we don't disrupt our ptracer.
    // NOTE: If we run into signal problems, we could consider only allowing a process to
    // send signals to itself, i.e., disallow inter-process signaling. See Github PR#1075.
    if (sig == SIGSTOP || sig == SIGCONT) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    trace(
        "making syscall %li in native thread %i (pid=%i and tid=%i)", syscallnum, my_tid, pid, tid);

    long result = 0;

    switch (syscallnum) {
        case SYS_kill: result = thread_nativeSyscall(sys->thread, SYS_kill, pid, sig); break;
        case SYS_tkill: result = thread_nativeSyscall(sys->thread, SYS_tkill, tid, sig); break;
        case SYS_tgkill:
            result = thread_nativeSyscall(sys->thread, SYS_tgkill, pid, tid, sig);
            break;
        default: utility_panic("Invalid syscall number %li given", syscallnum); break;
    }

    int error = syscall_rawReturnValueToErrno(result);

    trace("native syscall returned error code %i", error);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -error};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_kill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("kill called on pid %i with signal %i", pid, sig);

    pid_t native_pid = 0;

    if (pid == -1 || pid == 0) {
        // Special pids do not need translation
        native_pid = pid;
    } else {
        // Translate from virtual to native pid
        // Support -pid for kill
        native_pid = (pid > 0) ? host_getNativeTID(sys->host, pid, 0)
                               : -host_getNativeTID(sys->host, -pid, 0);

        // If there is no such thread, it's an error
        if (native_pid == 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
        }
    }

    trace("translated virtual pid %i to native pid %i", pid, native_pid);
    return _syscallhandler_killHelper(sys, native_pid, 0, sig, SYS_kill);
}

SysCallReturn syscallhandler_tgkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    pid_t tgid = args->args[0].as_i64;
    pid_t tid = args->args[1].as_i64;
    int sig = args->args[2].as_i64;

    trace("tgkill called on tgid %i and tid %i with signal %i", tgid, tid, sig);

    // Translate from virtual to native tgid and tid
    pid_t native_tgid = host_getNativeTID(sys->host, tgid, 0);
    pid_t native_tid = host_getNativeTID(sys->host, tgid, tid);

    // If there is no such threads it's an error
    if (native_tgid == 0 || native_tid == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    trace("translated virtual tgid %i to native tgid %i and virtual tid %i to native tid %i", tgid,
          native_tgid, tid, native_tid);
    return _syscallhandler_killHelper(sys, native_tgid, native_tid, sig, SYS_tgkill);
}

SysCallReturn syscallhandler_tkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t tid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("tkill called on tid %i with signal %i", tid, sig);

    // Translate from virtual to native tid
    pid_t native_tid = host_getNativeTID(sys->host, 0, tid);

    // If there is no such thread it's an error
    if (native_tid == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    trace("translated virtual tid %i to native tid %i", tid, native_tid);
    return _syscallhandler_killHelper(sys, 0, native_tid, sig, SYS_tkill);
}

// Removes `signal` from the sigset_t pointed to by `maskPtr`, if present.
// Returns 0 on success (including if the signal wasn't present), or a negative
// errno on failure.
static int _removeSignalFromSet(SysCallHandler* sys, PluginPtr maskPtr, int signal) {
    sigset_t mask;
    int rv = process_readPtr(sys->process, &mask, maskPtr, sizeof(mask));
    if (rv < 0) {
        trace("Error reading %p: %s", (void*)maskPtr.val, g_strerror(-rv));
        return rv;
    }
    rv = sigismember(&mask, SIGSYS);
    if (rv < 0) {
        panic("sigismember: %s", g_strerror(errno));
    }
    if (!rv) {
        trace("Signal %d wasn't in set", signal);
        return 0;
    }
    trace("Clearing %d from sigprocmask(SIG_BLOCK) set", signal);
    rv = sigdelset(&mask, SIGSYS);
    if (rv < 0) {
        panic("sigdelset: %s", g_strerror(errno));
    }
    rv = process_writePtr(sys->process, maskPtr, &mask, sizeof(mask));
    if (rv < 0) {
        trace("Error writing %p: %s", (void*)maskPtr.val, g_strerror(-rv));
        return rv;
    }
    return 0;
}

SysCallReturn syscallhandler_sigaction(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Prevent interference with shim's SIGSYS handler.

    int signum = (int)args->args[0].as_i64;
    PluginPtr sigaction_ptr = args->args[1].as_ptr;
    PluginPtr sa_mask_ptr = (PluginPtr){sigaction_ptr.val + offsetof(struct sigaction, sa_mask)};

    if (signum == SIGSYS) {
        warning("Blocking `sigaction` for SIGSYS");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -ENOSYS};
    }
    _removeSignalFromSet(sys, sa_mask_ptr, SIGSYS);

    return (SysCallReturn){.state = SYSCALL_NATIVE};
}

SysCallReturn syscallhandler_rt_sigaction(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Prevent interference with shim's SIGSYS handler.

    int signum = (int)args->args[0].as_i64;
    PluginPtr sigaction_ptr = args->args[1].as_ptr;
    PluginPtr sa_mask_ptr = (PluginPtr){sigaction_ptr.val + offsetof(struct sigaction, sa_mask)};

    if (signum == SIGSYS) {
        warning("Blocking `rt_sigaction` for SIGSYS");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -ENOSYS};
    }
    _removeSignalFromSet(sys, sa_mask_ptr, SIGSYS);

    return (SysCallReturn){.state = SYSCALL_NATIVE};
}

SysCallReturn syscallhandler_signal(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Prevent interference with shim's SIGSYS handler.

    int signum = (int)args->args[0].as_i64;

    if (signum == SIGSYS) {
        warning("Blocking `signal` for SIGSYS");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -ENOSYS};
    }
    return (SysCallReturn){.state = SYSCALL_NATIVE};
}

SysCallReturn syscallhandler_sigprocmask(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Prevent interference with shim's SIGSYS handler.

    int how = (int)args->args[0].as_i64;
    PluginPtr maskPtr = args->args[1].as_ptr;

    if (how == SIG_BLOCK || how == SIG_SETMASK) {
        int rv = _removeSignalFromSet(sys, maskPtr, SIGSYS);
        if (rv != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
    }
    return (SysCallReturn){.state = SYSCALL_NATIVE};
}

SysCallReturn syscallhandler_rt_sigprocmask(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Prevent interference with shim's SIGSYS handler.

    int how = (int)args->args[0].as_i64;
    PluginPtr maskPtr = args->args[1].as_ptr;

    if (how == SIG_BLOCK || how == SIG_SETMASK) {
        int rv = _removeSignalFromSet(sys, maskPtr, SIGSYS);
        if (rv != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
    }
    return (SysCallReturn){.state = SYSCALL_NATIVE};
}
