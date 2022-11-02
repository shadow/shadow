/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <signal.h>
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
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (sig > SHD_STANDARD_SIGNAL_MAX_NO) {
        warning("Unimplemented signal %d", sig);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    siginfo_t siginfo = {
        .si_signo = sig,
        .si_errno = 0,
        .si_code = SI_USER,
        .si_pid = process_getProcessID(sys->process),
        .si_uid = 0,
    };

    process_signal(process, sys->thread, &siginfo);

    return syscallreturn_makeDoneI64(0);
}

static SysCallReturn _syscallhandler_signalThread(SysCallHandler* sys, Thread* thread, int sig) {
    if (sig < 0 || sig > SHD_SIGRT_MAX) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (sig > SHD_STANDARD_SIGNAL_MAX_NO) {
        warning("Unimplemented signal %d", sig);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    if (sig == 0) {
        return syscallreturn_makeDoneI64(0);
    }

    Process* process = thread_getProcess(thread);
    struct shd_kernel_sigaction action = shimshmem_getSignalAction(
        host_getShimShmemLock(_syscallhandler_getHost(sys)), process_getSharedMem(process), sig);
    if (action.u.ksa_handler == SIG_IGN ||
        (action.u.ksa_handler == SIG_DFL &&
         shd_defaultAction(sig) == SHD_KERNEL_DEFAULT_ACTION_IGN)) {
        // Don't deliver ignored an signal.
        return syscallreturn_makeDoneI64(0);
    }

    shd_kernel_sigset_t pending_signals = shimshmem_getThreadPendingSignals(
        host_getShimShmemLock(_syscallhandler_getHost(sys)), thread_sharedMem(thread));

    if (shd_sigismember(&pending_signals, sig)) {
        // Signal is already pending. From signal(7):In the case where a standard signal is already
        // pending, the siginfo_t structure (see sigaction(2)) associated with  that  signal is not
        // overwritten on arrival of subsequent instances of the same signal.
        return syscallreturn_makeDoneI64(0);
    }

    shd_sigaddset(&pending_signals, sig);
    shimshmem_setThreadPendingSignals(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                                      thread_sharedMem(thread), pending_signals);
    shimshmem_setThreadSiginfo(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                               thread_sharedMem(thread), sig,
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
        return syscallreturn_makeDoneI64(0);
    }

    shd_kernel_sigset_t blocked_signals = shimshmem_getBlockedSignals(
        host_getShimShmemLock(_syscallhandler_getHost(sys)), thread_sharedMem(thread));
    if (shd_sigismember(&blocked_signals, sig)) {
        // Target thread has the signal blocked. We'll leave it pending, but no
        // need to schedule an event to process the signal. It'll get processed
        // synchronously when the thread executes a syscall that would unblock
        // the signal.
        return syscallreturn_makeDoneI64(0);
    }

    SysCallCondition* cond = thread_getSysCallCondition(thread);
    if (cond == NULL) {
        // We may be able to get here if a thread is signalled before it runs
        // for the first time. Just return; the signal will be delivered when
        // the thread runs.
        return syscallreturn_makeDoneI64(0);
    }
    syscallcondition_wakeupForSignal(
        cond, host_getShimShmemLock(_syscallhandler_getHost(sys)), sig);

    return syscallreturn_makeDoneI64(0);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_kill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
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
        return syscallreturn_makeDoneErrno(ENOSYS);
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

    Process* process = host_getProcess(_syscallhandler_getHost(sys), pid);
    if (process == NULL) {
        debug("Process %d not found", pid);
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    return _syscallhandler_signalProcess(sys, process, sig);
}

SysCallReturn syscallhandler_tgkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    pid_t tgid = args->args[0].as_i64;
    pid_t tid = args->args[1].as_i64;
    int sig = args->args[2].as_i64;

    trace("tgkill called on tgid %i and tid %i with signal %i", tgid, tid, sig);

    Thread* thread = host_getThread(_syscallhandler_getHost(sys), tid);
    if (thread == NULL) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    Process* process = thread_getProcess(thread);

    if (process_getProcessID(process) != tgid) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    return _syscallhandler_signalThread(sys, thread, sig);
}

SysCallReturn syscallhandler_tkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    pid_t tid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("tkill called on tid %i with signal %i", tid, sig);

    Thread* thread = host_getThread(_syscallhandler_getHost(sys), tid);
    if (thread == NULL) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    SysCallReturn ret = _syscallhandler_signalThread(sys, thread, sig);
    return ret;
}

static SysCallReturn _rt_sigaction(SysCallHandler* sys, int signum, PluginPtr actPtr,
                                   PluginPtr oldActPtr, size_t masksize) {
    utility_debugAssert(sys);

    if (signum < 1 || signum > 64) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (masksize != 64 / 8) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (oldActPtr.val) {
        struct shd_kernel_sigaction old_action =
            shimshmem_getSignalAction(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                                      process_getSharedMem(sys->process), signum);
        int rv = process_writePtr(sys->process, oldActPtr, &old_action, sizeof(old_action));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    if (actPtr.val) {
        if (signum == SIGKILL || signum == SIGSTOP) {
            return syscallreturn_makeDoneErrno(EINVAL);
        }

        struct shd_kernel_sigaction new_action;
        int rv = process_readPtr(sys->process, &new_action, actPtr, sizeof(new_action));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
        shimshmem_setSignalAction(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                                  process_getSharedMem(sys->process), signum, &new_action);
    }

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_rt_sigaction(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    SysCallReturn ret =
        _rt_sigaction(sys, /*signum=*/(int)args->args[0].as_i64,
                      /*actPtr=*/args->args[1].as_ptr,
                      /*oldActPtr=*/args->args[2].as_ptr, /*masksize=*/args->args[3].as_u64);
    return ret;
}

SysCallReturn syscallhandler_sigaltstack(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    PluginPtr ss_ptr = args->args[0].as_ptr;
    PluginPtr old_ss_ptr = args->args[1].as_ptr;
    trace("sigaltstack(%p, %p)", (void*)ss_ptr.val, (void*)old_ss_ptr.val);

    stack_t old_ss = shimshmem_getSigAltStack(
        host_getShimShmemLock(_syscallhandler_getHost(sys)), thread_sharedMem(sys->thread));

    if (ss_ptr.val) {
        if (old_ss.ss_flags & SS_ONSTACK) {
            // sigaltstack(2):  EPERM  An attempt was made to change the
            // alternate signal stack while it was active.
            return syscallreturn_makeDoneErrno(EPERM);
        }

        stack_t new_ss;
        int rv = process_readPtr(sys->process, &new_ss, ss_ptr, sizeof(new_ss));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
        if (new_ss.ss_flags & SS_DISABLE) {
            // sigaltstack(2): To disable an existing stack, specify ss.ss_flags
            // as SS_DISABLE.  In this case, the kernel ignores any other flags
            // in ss.ss_flags and the remaining fields in ss.
            new_ss = (stack_t){.ss_flags = SS_DISABLE};
        }
        if (new_ss.ss_flags & ~(SS_DISABLE | SS_AUTODISARM)) {
            // Unrecognized flag.
            return syscallreturn_makeDoneErrno(EINVAL);
        }
        shimshmem_setSigAltStack(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                                 thread_sharedMem(sys->thread), new_ss);
    }

    if (old_ss_ptr.val) {
        int rv = process_writePtr(sys->process, old_ss_ptr, &old_ss, sizeof(old_ss));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    return syscallreturn_makeDoneI64(0);
}

static SysCallReturn _rt_sigprocmask(SysCallHandler* sys, int how, PluginPtr setPtr,
                                     PluginPtr oldSetPtr, size_t sigsetsize) {
    utility_debugAssert(sys);

    // From sigprocmask(2): This argument is currently required to have a fixed architecture
    // specific value (equal to sizeof(kernel_sigset_t)).
    if (sigsetsize != (64 / 8)) {
        warning("Bad sigsetsize %zu", sigsetsize);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    shd_kernel_sigset_t current_set = shimshmem_getBlockedSignals(
        host_getShimShmemLock(_syscallhandler_getHost(sys)), thread_sharedMem(sys->thread));

    if (oldSetPtr.val) {
        int rv = process_writePtr(sys->process, oldSetPtr, &current_set, sizeof(current_set));
        if (rv < 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    if (setPtr.val) {
        shd_kernel_sigset_t set;
        int rv = process_readPtr(sys->process, &set, setPtr, sizeof(set));
        if (rv < 0) {
            return syscallreturn_makeDoneErrno(-rv);
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
                return syscallreturn_makeDoneErrno(EINVAL);
            }
        }

        shimshmem_setBlockedSignals(host_getShimShmemLock(_syscallhandler_getHost(sys)),
                                    thread_sharedMem(sys->thread), current_set);
    }

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_rt_sigprocmask(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    SysCallReturn ret =
        _rt_sigprocmask(sys, /*how=*/(int)args->args[0].as_i64, /*setPtr=*/args->args[1].as_ptr,
                        /*oldSetPtr=*/args->args[2].as_ptr, /*sigsetsize=*/args->args[3].as_u64);

    return ret;
}