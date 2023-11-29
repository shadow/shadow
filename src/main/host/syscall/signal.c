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
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
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

static SyscallReturn _syscallhandler_signalProcess(SyscallHandler* sys, const Process* process,
                                                   int sig) {
    if (sig == 0) {
        return syscallreturn_makeDoneI64(0);
    }

    if (!linux_signal_is_valid(sig)) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (linux_signal_is_realtime(sig)) {
        warning("Unimplemented signal %d", sig);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    pid_t processId = process_getProcessID(rustsyscallhandler_getProcess(sys));
    linux_siginfo_t siginfo = linux_siginfo_new_for_kill(sig, processId, 0);

    process_signal(process, rustsyscallhandler_getThread(sys), &siginfo);

    return syscallreturn_makeDoneI64(0);
}

static SyscallReturn _syscallhandler_signalThread(SyscallHandler* sys, const Thread* thread,
                                                  int sig) {
    if (sig == 0) {
        return syscallreturn_makeDoneI64(0);
    }

    if (!linux_signal_is_valid(sig)) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (linux_signal_is_realtime(sig)) {
        warning("Unimplemented signal %d", sig);
        return syscallreturn_makeDoneErrno(ENOSYS);
    }

    const Process* process = thread_getProcess(thread);
    struct linux_sigaction action = shimshmem_getSignalAction(
        host_getShimShmemLock(rustsyscallhandler_getHost(sys)), process_getSharedMem(process), sig);
    if (action.lsa_handler == SIG_IGN ||
        (action.lsa_handler == SIG_DFL && linux_defaultAction(sig) == LINUX_DEFAULT_ACTION_IGN)) {
        // Don't deliver ignored an signal.
        return syscallreturn_makeDoneI64(0);
    }

    linux_sigset_t pending_signals = shimshmem_getThreadPendingSignals(
        host_getShimShmemLock(rustsyscallhandler_getHost(sys)), thread_sharedMem(thread));

    if (linux_sigismember(&pending_signals, sig)) {
        // Signal is already pending. From signal(7):In the case where a standard signal is already
        // pending, the siginfo_t structure (see sigaction(2)) associated with  that  signal is not
        // overwritten on arrival of subsequent instances of the same signal.
        return syscallreturn_makeDoneI64(0);
    }

    linux_sigaddset(&pending_signals, sig);
    shimshmem_setThreadPendingSignals(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                      thread_sharedMem(thread), pending_signals);
    pid_t processId = process_getProcessID(rustsyscallhandler_getProcess(sys));
    linux_siginfo_t info = linux_siginfo_new_for_tkill(sig, processId, 0);
    shimshmem_setThreadSiginfo(
        host_getShimShmemLock(rustsyscallhandler_getHost(sys)), thread_sharedMem(thread), sig, &info);

    pid_t threadId = thread_getID(rustsyscallhandler_getThread(sys));

    if (thread_getID(thread) == threadId) {
        // Target is the current thread. It'll be handled synchronously when the
        // current syscall returns (if it's unblocked).
        return syscallreturn_makeDoneI64(0);
    }

    linux_sigset_t blocked_signals = shimshmem_getBlockedSignals(
        host_getShimShmemLock(rustsyscallhandler_getHost(sys)), thread_sharedMem(thread));
    if (linux_sigismember(&blocked_signals, sig)) {
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
    syscallcondition_wakeupForSignal(cond, rustsyscallhandler_getHost(sys), sig);

    return syscallreturn_makeDoneI64(0);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_kill(SyscallHandler* sys, const SysCallArgs* args) {
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
        pid = process_getProcessID(rustsyscallhandler_getProcess(sys));
    } else if (pid < -1) {
        // kill(2): If pid is less than -1, then sig is sent to every process in
        // the process group whose ID is -pid.
        //
        // Currently every emulated process is in its own process group, where
        // pgid=pid.
        pid = -pid;
    }

    const Process* process = host_getProcess(rustsyscallhandler_getHost(sys), pid);
    if (process == NULL) {
        debug("Process %d not found", pid);
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    return _syscallhandler_signalProcess(sys, process, sig);
}

SyscallReturn syscallhandler_tgkill(SyscallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    pid_t tgid = args->args[0].as_i64;
    pid_t tid = args->args[1].as_i64;
    int sig = args->args[2].as_i64;

    trace("tgkill called on tgid %i and tid %i with signal %i", tgid, tid, sig);

    const Thread* thread = host_getThread(rustsyscallhandler_getHost(sys), tid);
    if (thread == NULL) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    const Process* process = thread_getProcess(thread);

    if (process_getProcessID(process) != tgid) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    return _syscallhandler_signalThread(sys, thread, sig);
}

SyscallReturn syscallhandler_tkill(SyscallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    pid_t tid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    trace("tkill called on tid %i with signal %i", tid, sig);

    const Thread* thread = host_getThread(rustsyscallhandler_getHost(sys), tid);
    if (thread == NULL) {
        return syscallreturn_makeDoneErrno(ESRCH);
    }

    SyscallReturn ret = _syscallhandler_signalThread(sys, thread, sig);
    return ret;
}

static SyscallReturn _rt_sigaction(SyscallHandler* sys, int signum, UntypedForeignPtr actPtr,
                                   UntypedForeignPtr oldActPtr, size_t masksize) {
    utility_debugAssert(sys);

    if (signum < 1 || signum > 64) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (masksize != 64 / 8) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (oldActPtr.val) {
        struct linux_sigaction old_action = shimshmem_getSignalAction(
            host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
            process_getSharedMem(rustsyscallhandler_getProcess(sys)), signum);
        int rv = process_writePtr(
            rustsyscallhandler_getProcess(sys), oldActPtr, &old_action, sizeof(old_action));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    if (actPtr.val) {
        if (signum == SIGKILL || signum == SIGSTOP) {
            return syscallreturn_makeDoneErrno(EINVAL);
        }

        struct linux_sigaction new_action;
        int rv = process_readPtr(
            rustsyscallhandler_getProcess(sys), &new_action, actPtr, sizeof(new_action));
        if (rv != 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
        shimshmem_setSignalAction(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                  process_getSharedMem(rustsyscallhandler_getProcess(sys)), signum,
                                  &new_action);
    }

    return syscallreturn_makeDoneI64(0);
}

SyscallReturn syscallhandler_rt_sigaction(SyscallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    SyscallReturn ret =
        _rt_sigaction(sys, /*signum=*/(int)args->args[0].as_i64,
                      /*actPtr=*/args->args[1].as_ptr,
                      /*oldActPtr=*/args->args[2].as_ptr, /*masksize=*/args->args[3].as_u64);
    return ret;
}

SyscallReturn syscallhandler_sigaltstack(SyscallHandler* sys, const SysCallArgs* args) {
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

static SyscallReturn _rt_sigprocmask(SyscallHandler* sys, int how, UntypedForeignPtr setPtr,
                                     UntypedForeignPtr oldSetPtr, size_t sigsetsize) {
    utility_debugAssert(sys);

    // From sigprocmask(2): This argument is currently required to have a fixed architecture
    // specific value (equal to sizeof(kernel_sigset_t)).
    if (sigsetsize != (64 / 8)) {
        warning("Bad sigsetsize %zu", sigsetsize);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    linux_sigset_t current_set =
        shimshmem_getBlockedSignals(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                    thread_sharedMem(rustsyscallhandler_getThread(sys)));

    if (oldSetPtr.val) {
        int rv = process_writePtr(
            rustsyscallhandler_getProcess(sys), oldSetPtr, &current_set, sizeof(current_set));
        if (rv < 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }
    }

    if (setPtr.val) {
        linux_sigset_t set;
        int rv = process_readPtr(rustsyscallhandler_getProcess(sys), &set, setPtr, sizeof(set));
        if (rv < 0) {
            return syscallreturn_makeDoneErrno(-rv);
        }

        switch (how) {
            case SIG_BLOCK: {
                current_set = linux_sigorset(&current_set, &set);
                break;
            }
            case SIG_UNBLOCK: {
                linux_sigset_t notset = linux_signotset(&set);
                current_set = linux_sigandset(&current_set, &notset);
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

        shimshmem_setBlockedSignals(host_getShimShmemLock(rustsyscallhandler_getHost(sys)),
                                    thread_sharedMem(rustsyscallhandler_getThread(sys)), current_set);
    }

    return syscallreturn_makeDoneI64(0);
}

SyscallReturn syscallhandler_rt_sigprocmask(SyscallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);

    SyscallReturn ret =
        _rt_sigprocmask(sys, /*how=*/(int)args->args[0].as_i64, /*setPtr=*/args->args[1].as_ptr,
                        /*oldSetPtr=*/args->args[2].as_ptr, /*sigsetsize=*/args->args[3].as_u64);

    return ret;
}
