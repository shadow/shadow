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
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
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

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SysCallReturn _syscallhandler_signalProcess(SysCallHandler* sys, Process* process, int sig) {
    if (sig < 0 || sig > SHD_SIGRT_MAX) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    if (sig > SHD_STANDARD_SIGNAL_MAX_NO) {
        warning("Unimplemented signal %d", sig);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    if (sig == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    if (!shimipc_getUseSeccomp()) {
        // ~legacy ptrace path. Send a real signal to the process.
        pid_t nativePid = process_getNativePid(process);
        long res = thread_nativeSyscall(sys->thread, SYS_kill, nativePid, sig);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = res};
    }

    struct shd_kernel_sigaction action =
        shimshmem_getSignalAction(sys->shimShmemHostLock, process_getSharedMem(process), sig);
    if (action.ksa_handler == SIG_IGN ||
        (action.ksa_handler == SIG_DFL && shd_defaultAction(sig) == SHD_DEFAULT_ACTION_IGN)) {
        // Don't deliver ignored an signal.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    shd_kernel_sigset_t pending_signals =
        shimshmem_getProcessPendingSignals(sys->shimShmemHostLock, process_getSharedMem(process));

    if (shd_sigismember(&pending_signals, sig)) {
        // Signal is already pending. From signal(7):In the case where a standard signal is already
        // pending, the siginfo_t structure (see sigaction(2)) associated with  that  signal is not
        // overwritten on arrival of subsequent instances of the same signal.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    shd_sigaddset(&pending_signals, sig);
    shimshmem_setProcessPendingSignals(
        sys->shimShmemHostLock, process_getSharedMem(process), pending_signals);
    shimshmem_setProcessSiginfo(sys->shimShmemHostLock, process_getSharedMem(process), sig,
                                &(siginfo_t){
                                    .si_signo = sig,
                                    .si_errno = 0,
                                    .si_code = SI_USER,
                                    .si_pid = process_getProcessID(sys->process),
                                    .si_uid = 0,
                                });

    if (process == sys->process) {
        shd_kernel_sigset_t blocked_signals =
            shimshmem_getBlockedSignals(sys->shimShmemHostLock, thread_sharedMem(sys->thread));
        if (!shd_sigismember(&blocked_signals, sig)) {
            // Target process is this process, and this thread hasn't blocked
            // the signal.  It will be delivered to this thread.
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
        }
    }

    process_interruptWithSignal(process, sys->shimShmemHostLock, sig);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

static SysCallReturn _syscallhandler_signalThread(SysCallHandler* sys, Thread* thread, int sig) {
    if (sig < 0 || sig > SHD_SIGRT_MAX) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    if (sig > SHD_STANDARD_SIGNAL_MAX_NO) {
        warning("Unimplemented signal %d", sig);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    }

    if (sig == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    if (!shimipc_getUseSeccomp()) {
        // ~legacy ptrace path. Send a real signal to the thread.
        pid_t nativeTid = thread_getNativePid(thread);
        Process* process = thread_getProcess(thread);
        pid_t nativePid = process_getNativePid(process);
        long res = thread_nativeSyscall(sys->thread, SYS_tgkill, nativePid, nativeTid, sig);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = res};
    }

    Process* process = thread_getProcess(thread);
    struct shd_kernel_sigaction action =
        shimshmem_getSignalAction(sys->shimShmemHostLock, process_getSharedMem(process), sig);
    if (action.ksa_handler == SIG_IGN ||
        (action.ksa_handler == SIG_DFL && shd_defaultAction(sig) == SHD_DEFAULT_ACTION_IGN)) {
        // Don't deliver ignored an signal.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    shd_kernel_sigset_t pending_signals =
        shimshmem_getThreadPendingSignals(sys->shimShmemHostLock, thread_sharedMem(thread));

    if (shd_sigismember(&pending_signals, sig)) {
        // Signal is already pending. From signal(7):In the case where a standard signal is already
        // pending, the siginfo_t structure (see sigaction(2)) associated with  that  signal is not
        // overwritten on arrival of subsequent instances of the same signal.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    shd_sigaddset(&pending_signals, sig);
    shimshmem_setThreadPendingSignals(
        sys->shimShmemHostLock, thread_sharedMem(thread), pending_signals);
    shimshmem_setThreadSiginfo(sys->shimShmemHostLock, thread_sharedMem(thread), sig,
                               &(siginfo_t){
                                   .si_signo = sig,
                                   .si_errno = 0,
                                   .si_code = SI_TKILL,
                                   .si_pid = process_getProcessID(sys->process),
                                   .si_uid = 0,
                               });

    if (thread == sys->thread) {
        // Target is the current thread. It'll be handled synchronously when the
        // current syscall returns (if it's unblocked).
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    shd_kernel_sigset_t blocked_signals =
        shimshmem_getBlockedSignals(sys->shimShmemHostLock, thread_sharedMem(thread));
    if (shd_sigismember(&blocked_signals, sig)) {
        // Target thread has the signal blocked. We'll leave it pending, but no
        // need to schedule an event to process the signal. It'll get processed
        // synchronously when the thread executes a syscall that would unblock
        // the signal.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }

    SysCallCondition* cond = thread_getSysCallCondition(thread);
    if (cond == NULL) {
        // We may be able to get here if a thread is signalled before it runs
        // for the first time. Just return; the signal will be delivered when
        // the thread runs.
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
    }
    syscallcondition_wakeupForSignal(cond, sys->shimShmemHostLock, sig);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_kill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("kill called on pid %i with signal %i", pid, sig);

    if (pid == -1) {
        // kill(2): If  pid  equals -1, then sig is sent to every process for
        // which the calling process has permission to send signals, except for
        // process 1.
        //
        // Currently unimplemented, and unlikely to be needed in the context of
        // a shadow simulation.
        warning("kill with pid=-1 unimplemented");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
    } else if (pid == 0) {
        // kill(2): If pid equals 0, then sig is sent to every process in the
        // process group of the calling process.
        //
        // Currently every emulated process is in its own process group.
        pid = process_getProcessID(sys->process);
    } else if (pid < -1) {
        // kill(2): If pid is less than -1, then sig is sent to every process in
        // the process group whose ID is -pid.
        //
        // Currently every emulated process is in its own process group, where
        // pgid=pid.
        pid = -pid;
    }

    Process* process = host_getProcess(sys->host, pid);
    if (process == NULL) {
        debug("Process %d not found", pid);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    return _syscallhandler_signalProcess(sys, process, sig);
}

SysCallReturn syscallhandler_tgkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    pid_t tgid = args->args[0].as_i64;
    pid_t tid = args->args[1].as_i64;
    int sig = args->args[2].as_i64;

    trace("tgkill called on tgid %i and tid %i with signal %i", tgid, tid, sig);

    Thread* thread = host_getThread(sys->host, tid);
    if (thread == NULL) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    Process* process = thread_getProcess(thread);

    if (process_getProcessID(process) != tgid) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    return _syscallhandler_signalThread(sys, thread, sig);
}

SysCallReturn syscallhandler_tkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t tid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("tkill called on tid %i with signal %i", tid, sig);

    Thread* thread = host_getThread(sys->host, tid);
    if (thread == NULL) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    SysCallReturn ret = _syscallhandler_signalThread(sys, thread, sig);
    return ret;
}

static SysCallReturn _rt_sigaction(SysCallHandler* sys, int signum, PluginPtr actPtr,
                                   PluginPtr oldActPtr, size_t masksize) {
    utility_assert(sys);

    if (!shimipc_getUseSeccomp()) {
        // No special handling needed.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    if (signum < 1 || signum > 64) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
    }

    if (masksize != 64 / 8) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
    }

    if (oldActPtr.val) {
        struct shd_kernel_sigaction old_action = shimshmem_getSignalAction(
            sys->shimShmemHostLock, process_getSharedMem(sys->process), signum);
        int rv = process_writePtr(sys->process, oldActPtr, &old_action, sizeof(old_action));
        if (rv != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
    }

    if (actPtr.val) {
        if (signum == SIGKILL || signum == SIGSTOP) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
        }

        struct shd_kernel_sigaction new_action;
        int rv = process_readPtr(sys->process, &new_action, actPtr, sizeof(new_action));
        if (rv != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
        shimshmem_setSignalAction(
            sys->shimShmemHostLock, process_getSharedMem(sys->process), signum, &new_action);
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval = 0};
}

SysCallReturn syscallhandler_rt_sigaction(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    SysCallReturn ret =
        _rt_sigaction(sys, /*signum=*/(int)args->args[0].as_i64,
                      /*actPtr=*/args->args[1].as_ptr,
                      /*oldActPtr=*/args->args[2].as_ptr, /*masksize=*/args->args[3].as_u64);
    return ret;
}

SysCallReturn syscallhandler_sigaltstack(SysCallHandler* sys, const SysCallArgs* args) {
    if (!shimipc_getUseSeccomp()) {
        // Outside of seccomp mode, just handle natively.
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }
    // Otherwise we need to emulate to ensure that the shim's own sigaltstack
    // configuration isn't clobbered.

    utility_assert(sys && args);
    PluginPtr ss_ptr = args->args[0].as_ptr;
    PluginPtr old_ss_ptr = args->args[1].as_ptr;
    trace("sigaltstack(%p, %p)", (void*)ss_ptr.val, (void*)old_ss_ptr.val);

    if (ss_ptr.val) {
        // TODO: if the alt stack is already active, return -EPERM.
        stack_t ss;
        int rv = process_readPtr(sys->process, &ss, ss_ptr, sizeof(ss));
        if (rv != 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
        trace("sigaltstack ss_sp:%p ss_flags:%x ss_size:%zu", ss.ss_sp, ss.ss_flags, ss.ss_size);
        // TODO: arrange for this info to be returned in subsequent calls via old_ss_ptr
        // and to actually use this stack in managed thread signal handlers.
        warning(
            "sigaltstack used, but is currently only partially emulated. Instability may result.");
    }

    if (old_ss_ptr.val) {
        // TODO: If an old stack was configured, return that instead of the native configuration.
        long result = thread_nativeSyscall(sys->thread, SYS_sigaltstack, NULL, old_ss_ptr.val);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = result};
    }

    // Ignore
    return (SysCallReturn){.state = SYSCALL_DONE, .retval = 0};
}

static SysCallReturn _rt_sigprocmask(SysCallHandler* sys, int how, PluginPtr setPtr,
                                     PluginPtr oldSetPtr, size_t sigsetsize) {
    utility_assert(sys);

    if (!shimipc_getUseSeccomp()) {
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    // From sigprocmask(2): This argument is currently required to have a fixed architecture
    // specific value (equal to sizeof(kernel_sigset_t)).
    if (sigsetsize != (64 / 8)) {
        warning("Bad sigsetsize %zu", sigsetsize);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
    }

    shd_kernel_sigset_t current_set =
        shimshmem_getBlockedSignals(sys->shimShmemHostLock, thread_sharedMem(sys->thread));

    if (oldSetPtr.val) {
        int rv = process_writePtr(sys->process, oldSetPtr, &current_set, sizeof(current_set));
        if (rv < 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }
    }

    if (setPtr.val) {
        shd_kernel_sigset_t set;
        int rv = process_readPtr(sys->process, &set, setPtr, sizeof(set));
        if (rv < 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval = rv};
        }

        switch (how) {
            case SIG_BLOCK: {
                current_set = shd_sigorset(&current_set, &set);
                break;
            }
            case SIG_UNBLOCK: {
                shd_kernel_sigset_t notset = shd_signotset(&set);
                current_set = shd_sigandset(&current_set, &notset);
                break;
            }
            case SIG_SETMASK: {
                current_set = set;
                break;
            }
            default: {
                return (SysCallReturn){.state = SYSCALL_DONE, .retval = -EINVAL};
            }
        }

        shimshmem_setBlockedSignals(
            sys->shimShmemHostLock, thread_sharedMem(sys->thread), current_set);
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval = 0};
}

SysCallReturn syscallhandler_rt_sigprocmask(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    SysCallReturn ret =
        _rt_sigprocmask(sys, /*how=*/(int)args->args[0].as_i64, /*setPtr=*/args->args[1].as_ptr,
                        /*oldSetPtr=*/args->args[2].as_ptr, /*sigsetsize=*/args->args[3].as_u64);

    return ret;
}