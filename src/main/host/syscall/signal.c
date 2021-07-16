/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/syscall.h>

#include "lib/logger/logger.h"
#include "main/host/host.h"
#include "main/host/shimipc.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
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

static bool _sigset_includes_shim_handled_signal(const sigset_t* set) {
    for (int i = 0; i < ARRAY_LENGTH(_shim_handled_signals); ++i) {
        int signal = _shim_handled_signals[i];
        int rv = sigismember(set, signal);
        if (rv < 0) {
            panic("sigismember: %s", g_strerror(errno));
        }
        if (rv) {
            return true;
        }
    }
    return false;
}

static void _sigset_remove_shim_handled_signals(sigset_t* set) {
    for (int i = 0; i < ARRAY_LENGTH(_shim_handled_signals); ++i) {
        int signal = _shim_handled_signals[i];
        if (sigdelset(set, signal) < 0) {
            panic("sigdelset: %s", g_strerror(errno));
        }
    }
}

// This should be compatible with the kernel's sigaction struct, which is what
// the syscalls take in cases where the userspace wrappers take `struct
// sigaction`. While a similar struct definition appears in the glibc source,
// it's not exposed to users, so we duplicate it here. While in principle this
// definition could change out from under us, it's more likely that the kernel
// would introduce a new syscall number rather than breaking userspace programs
// that still had the old definition.
//
// When reading/writing a userspace pointer, use kernel_sigaction_size to
// calculate the true size. This is needed because the last field, `sa_mask`,
// is actually variable size, and is specified with another parameter in the syscall.
struct kernel_sigaction {
    void* handler;
    unsigned long sa_flags;
    void (*sa_restorer)(void);
    sigset_t sa_mask;
};

// Calculates the size of a corresponding `struct kernel_sigaction`. Returns 0
// if the masksize is invalid.
static size_t kernel_sigaction_size(size_t masksize) {
    if (masksize < 4) {
        warning("Got bad masksize: %zu < 4", masksize);
    }
    size_t sz = sizeof(struct kernel_sigaction) - sizeof(sigset_t) + masksize;
    if (sz > sizeof(struct kernel_sigaction)) {
        warning("Got bad sigaction size: %zu > %zu", sz, sizeof(struct kernel_sigaction));
        return 0;
    }
    return sz;
}

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

static SysCallReturn _rt_sigaction(SysCallHandler* sys, int signum, PluginPtr actPtr,
                                   PluginPtr oldActPtr, size_t masksize) {
    utility_assert(sys);

    if (!shimipc_getUseSeccomp()) {
        // No special handling needed.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    // Prevent interference with shim's signal handlers.

    if (!actPtr.val) {
        // Caller is just reading the action; allow to proceed natively.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    for (int i = 0; i < ARRAY_LENGTH(_shim_handled_signals); ++i) {
        int shim_signal = _shim_handled_signals[i];
        if (signum == shim_signal) {
            warning("Ignoring `sigaction` for signal %d", shim_signal);
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = 0};
        }
    }

    size_t sz = kernel_sigaction_size(masksize);
    if (!sz) {
        warning("Bad masksize %zu", masksize);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
    }

    struct kernel_sigaction action = {0};
    int rv = process_readPtr(sys->process, &action, actPtr, sz);
    if (rv < 0) {
        warning("Couldn't read action ptr %p", (void*)actPtr.val);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
    }

    if (!_sigset_includes_shim_handled_signal(&action.sa_mask)) {
        // SIGSYS not present; proceed natively.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    // We can't safely modify actPtr, particularly since it could be read-only memory.
    // We need to set up our own struct and make the syscall using that.

    AllocdMem_u8* modified_action_mem = allocdmem_new(sys->thread, sizeof(struct kernel_sigaction));
    utility_assert(modified_action_mem);
    PluginPtr modified_action_ptr = allocdmem_pluginPtr(modified_action_mem);
    struct kernel_sigaction* modified_action =
        process_getWriteablePtr(sys->process, modified_action_ptr, sizeof(struct kernel_sigaction));
    utility_assert(modified_action);

    *modified_action = action;
    _sigset_remove_shim_handled_signals(&modified_action->sa_mask);
    process_flushPtrs(sys->process);

    long result = thread_nativeSyscall(
        sys->thread, SYS_rt_sigaction, signum, modified_action_ptr, oldActPtr, masksize);
    trace(
        "rt_sigaction returned %ld, error:%s", result, result >= 0 ? "none" : g_strerror(-result));

    allocdmem_free(sys->thread, modified_action_mem);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval = result};
}

SysCallReturn syscallhandler_rt_sigaction(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    return _rt_sigaction(sys, /*signum=*/(int)args->args[0].as_i64, /*actPtr=*/args->args[1].as_ptr,
                         /*oldActPtr=*/args->args[2].as_ptr, /*masksize=*/args->args[3].as_u64);
}

static SysCallReturn _rt_sigprocmask(SysCallHandler* sys, int how, PluginPtr setPtr,
                                     PluginPtr oldSetPtr, size_t sigsetsize) {
    utility_assert(sys);

    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    // Prevent interference with shim's SIGSYS handler.

    if (!setPtr.val) {
        // Not writing; allow natively.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    if (how != SIG_BLOCK) {
        // Not blocking; allow natively.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    if (sigsetsize < 4 || sigsetsize > sizeof(sigset_t)) {
        warning("Bad sigsetsize %zu", sigsetsize);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
    }

    sigset_t set = {0};
    int rv = process_readPtr(sys->process, &set, setPtr, sigsetsize);
    if (rv < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
    }

    if (!_sigset_includes_shim_handled_signal(&set)) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    // Remove handled signals from set and execute syscall with modified set.

    AllocdMem_u8* modified_set_mem = allocdmem_new(sys->thread, sizeof(sigset_t));
    utility_assert(modified_set_mem);
    PluginPtr modified_set_ptr = allocdmem_pluginPtr(modified_set_mem);
    sigset_t* modified_set =
        process_getWriteablePtr(sys->process, modified_set_ptr, sizeof(*modified_set));
    utility_assert(modified_set);

    *modified_set = set;
    _sigset_remove_shim_handled_signals(modified_set);
    process_flushPtrs(sys->process);

    long result = thread_nativeSyscall(
        sys->thread, SYS_rt_sigprocmask, how, modified_set_ptr, oldSetPtr, sigsetsize);
    trace("rt_sigprocmask returned %ld, error:%s", result,
          result >= 0 ? "none" : g_strerror(-result));

    allocdmem_free(sys->thread, modified_set_mem);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval = result};
}

SysCallReturn syscallhandler_rt_sigprocmask(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    return _rt_sigprocmask(sys, /*how=*/(int)args->args[0].as_i64, /*setPtr=*/args->args[1].as_ptr,
                           /*oldSetPtr=*/args->args[2].as_ptr, /*sigsetsize=*/args->args[3].as_u64);
}
