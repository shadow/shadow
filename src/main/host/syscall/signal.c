/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/syscall.h>

#include "main/host/host.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "main/utility/syscall.h"
#include "support/logger/logger.h"

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

    debug(
        "making syscall %li in native thread %i (pid=%i and tid=%i)", syscallnum, my_tid, pid, tid);

    long result = 0;

    switch (syscallnum) {
        case SYS_kill: result = thread_nativeSyscall(sys->thread, SYS_kill, pid, sig); break;
        case SYS_tkill: result = thread_nativeSyscall(sys->thread, SYS_tkill, tid, sig); break;
        case SYS_tgkill:
            result = thread_nativeSyscall(sys->thread, SYS_tgkill, pid, tid, sig);
            break;
        default: error("Invalid syscall number %li given", syscallnum); break;
    }

    int error = syscall_rawReturnValueToErrno(result);

    debug("native syscall returned error code %i", error);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -error};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_kill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t pid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    debug("kill called on pid %i with signal %i", pid, sig);

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

    debug("translated virtual pid %i to native pid %i", pid, native_pid);
    return _syscallhandler_killHelper(sys, native_pid, 0, sig, SYS_kill);
}

SysCallReturn syscallhandler_tgkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    pid_t tgid = args->args[0].as_i64;
    pid_t tid = args->args[1].as_i64;
    int sig = args->args[2].as_i64;

    debug("tgkill called on tgid %i and tid %i with signal %i", tgid, tid, sig);

    // Translate from virtual to native tgid and tid
    pid_t native_tgid = host_getNativeTID(sys->host, tgid, 0);
    pid_t native_tid = host_getNativeTID(sys->host, tgid, tid);

    // If there is no such threads it's an error
    if (native_tgid == 0 || native_tid == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    debug("translated virtual tgid %i to native tgid %i and virtual tid %i to native tid %i", tgid,
          native_tgid, tid, native_tid);
    return _syscallhandler_killHelper(sys, native_tgid, native_tid, sig, SYS_tgkill);
}

SysCallReturn syscallhandler_tkill(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);
    pid_t tid = args->args[0].as_i64;
    int sig = args->args[1].as_i64;

    debug("tkill called on tid %i with signal %i", tid, sig);

    // Translate from virtual to native tid
    pid_t native_tid = host_getNativeTID(sys->host, 0, tid);

    // If there is no such thread it's an error
    if (native_tid == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESRCH};
    }

    debug("translated virtual tid %i to native tid %i", tid, native_tid);
    return _syscallhandler_killHelper(sys, 0, native_tid, sig, SYS_tkill);
}
