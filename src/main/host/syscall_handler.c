/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/process.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_numbers.h"
#include "main/host/syscall_types.h"
#include "main/utility/syscall.h"

// Not defined in some older libc's.
#ifndef SYS_rseq
#define SYS_rseq 334
#endif
#ifndef SYS_epoll_pwait2
#define SYS_epoll_pwait2 441
#endif

static bool _countSyscalls = false;
ADD_CONFIG_HANDLER(config_getUseSyscallCounters, _countSyscalls)

const Host* _syscallhandler_getHost(const SysCallHandler* sys) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == sys->hostId);
    return host;
}

const Process* _syscallhandler_getProcess(const SysCallHandler* sys) {
    const Process* process = worker_getCurrentProcess();
    utility_debugAssert(process_getProcessID(process) == sys->processId);
    return process;
}

const char* _syscallhandler_getProcessName(const SysCallHandler* sys) {
    const Process* process = worker_getCurrentProcess();
    utility_debugAssert(process_getProcessID(process) == sys->processId);
    return process_getPluginName(process);
}

const Thread* _syscallhandler_getThread(const SysCallHandler* sys) {
    const Thread* thread = worker_getCurrentThread();
    utility_debugAssert(thread_getID(thread) == sys->threadId);
    return thread;
}

SysCallHandler* syscallhandler_new(HostId hostId, pid_t processId, pid_t threadId) {
    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .hostId = hostId,
        .processId = processId,
        .threadId = threadId,
        .syscall_handler_rs = rustsyscallhandler_new(),
        .blockedSyscallNR = -1,
        // Like the timer above, we use an epoll object for servicing
        // some syscalls, and so we won't assign it a fd handle.
        .epoll = epoll_new(),
#ifdef USE_PERF_TIMERS
        // Used to track syscall handler performance
        .perfTimer = g_timer_new(),
#endif
    };

    if (_countSyscalls) {
        sys->syscall_counter = counter_new();
    }

    MAGIC_INIT(sys);

    worker_count_allocation(SysCallHandler);
    return sys;
}

void syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

#ifdef USE_PERF_TIMERS
    debug("handled %li syscalls in %f seconds", sys->numSyscalls, sys->perfSecondsTotal);
#else
    debug("handled %li syscalls", sys->numSyscalls);
#endif

    if (_countSyscalls && sys->syscall_counter) {
        // Log the plugin thread specific counts
        char* str = counter_alloc_string(sys->syscall_counter);
        debug("Thread %d syscall counts: %s", sys->threadId, str);
        counter_free_string(sys->syscall_counter, str);

        // Add up the counts at the worker level
        worker_add_syscall_counts(sys->syscall_counter);

        // Cleanup
        counter_free(sys->syscall_counter);
    }

    if (sys->syscall_handler_rs) {
        rustsyscallhandler_free(sys->syscall_handler_rs);
    }

    if (sys->epoll) {
        legacyfile_unref(sys->epoll);
    }
#ifdef USE_PERF_TIMERS
    if (sys->perfTimer) {
        g_timer_destroy(sys->perfTimer);
    }
#endif

    MAGIC_CLEAR(sys);
    free(sys);
    worker_count_deallocation(SysCallHandler);
}

static void _syscallhandler_pre_syscall(SysCallHandler* sys, long number,
                                        const char* name) {
    trace("SYSCALL_HANDLER_PRE(%s,pid=%u): handling syscall %ld %s%s",
          _syscallhandler_getProcessName(sys), sys->threadId, number, name,
          _syscallhandler_wasBlocked(sys) ? " (previously BLOCKed)" : "");

    // Count the frequency of each syscall, but only on the initial call.
    // This avoids double counting in the case where the initial call blocked at first,
    // but then later became unblocked and is now being handled again here.
    if (sys->syscall_counter && !_syscallhandler_wasBlocked(sys)) {
        counter_add_value(sys->syscall_counter, name, 1);
    }

#ifdef USE_PERF_TIMERS
    /* Track elapsed time during this syscall by marking the start time. */
    g_timer_start(sys->perfTimer);
#endif
}

static void _syscallhandler_post_syscall(SysCallHandler* sys, long number, const char* name,
                                         SyscallReturn* scr) {
#ifdef USE_PERF_TIMERS
    /* Add the cumulative elapsed seconds and num syscalls. */
    sys->perfSecondsCurrent += g_timer_elapsed(sys->perfTimer, NULL);
#endif
    if (logger_isEnabled(logger_getDefault(), LOGLEVEL_TRACE)) {
        const char* errstr = "n/a";
        char errstrbuf[100];

        const char* valstr = "n/a";
        char valbuf[100];
        if (scr->tag == SYSCALL_RETURN_DONE) {
            SyscallReturnDone* done = syscallreturn_done(scr);
            if (done->retval.as_i64 < 0) {
                errstr = strerror_r(-done->retval.as_i64, errstrbuf, sizeof(errstrbuf));
            }
            snprintf(valbuf, sizeof(valbuf), "%" PRIi64, done->retval.as_i64);
            valstr = valbuf;
        }
        trace("SYSCALL_HANDLER_POST(%s,pid=%u): syscall %ld %s result: state=%s%s "
              "val=%s(%s)",
              _syscallhandler_getProcessName(sys), sys->threadId, number, name,
              _syscallhandler_wasBlocked(sys) ? "BLOCK->" : "", syscallreturnstate_str(scr->tag),
              valstr, errstr);
    }

#ifdef USE_PERF_TIMERS
    debug("handling syscall %ld %s took %f seconds", number, name, sys->perfSecondsCurrent);
#endif

    if (scr->tag != SYSCALL_RETURN_BLOCK) {
        /* The syscall completed, count it and the cumulative time to complete it. */
        sys->numSyscalls++;
#ifdef USE_PERF_TIMERS
        sys->perfSecondsTotal += sys->perfSecondsCurrent;
        sys->perfSecondsCurrent = 0;
#endif
    }

    // We need to flush pointers here, so that the syscall formatter can
    // reliably borrow process memory without an incompatible borrow.
    if (!(scr->tag == SYSCALL_RETURN_DONE &&
          syscall_rawReturnValueToErrno(syscallreturn_done(scr)->retval.as_i64) == 0)) {
        // The syscall didn't complete successfully; don't write back pointers.
        trace("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
        process_freePtrsWithoutFlushing(_syscallhandler_getProcess(sys));
    } else {
        int res = process_flushPtrs(_syscallhandler_getProcess(sys));
        if (res != 0) {
            panic("Flushing syscall ptrs: %s", g_strerror(-res));
        }
    }
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

#define HANDLE_RUST(s)                                                                             \
    case SYS_##s: {                                                                                \
        _syscallhandler_pre_syscall(sys, args->number, #s);                                        \
        SyscallHandler* handler = sys->syscall_handler_rs;                                         \
        sys->syscall_handler_rs = NULL;                                                            \
        scr = rustsyscallhandler_syscall(handler, sys, args);                                      \
        sys->syscall_handler_rs = handler;                                                         \
        _syscallhandler_post_syscall(sys, args->number, #s, &scr);                                 \
    } break

SyscallReturn syscallhandler_make_syscall(SysCallHandler* sys, const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    StraceFmtMode straceLoggingMode = process_straceLoggingMode(_syscallhandler_getProcess(sys));
    const Host* host = _syscallhandler_getHost(sys);
    const Process* process = _syscallhandler_getProcess(sys);
    const Thread* thread = _syscallhandler_getThread(sys);

    SyscallReturn scr;

    /* Make sure that we either don't have a blocked syscall,
     * or if we blocked a syscall, then that same syscall
     * should be executed again when it becomes unblocked. */
    if (sys->blockedSyscallNR >= 0 && sys->blockedSyscallNR != args->number) {
        utility_panic("We blocked syscall number %ld but syscall number %ld "
                      "is unexpectedly being invoked",
                      sys->blockedSyscallNR, args->number);
    }

    if (sys->havePendingResult) {
        // The syscall was already completed, but we delayed the response to yield the CPU.
        // Return that response now.
        trace("Returning delayed result");
        sys->havePendingResult = false;
        utility_debugAssert(sys->pendingResult.tag != SYSCALL_RETURN_BLOCK);
        sys->blockedSyscallNR = -1;
        return sys->pendingResult;
    } else {
        switch (args->number) {
            HANDLE_RUST(accept);
            HANDLE_RUST(accept4);
            HANDLE_RUST(bind);
            HANDLE_RUST(brk);
            HANDLE_RUST(clock_getres);
            HANDLE_RUST(clock_gettime);
            HANDLE_RUST(clock_nanosleep);
            HANDLE_RUST(clone);
#ifdef SYS_clone3
            HANDLE_RUST(clone3);
#endif
            HANDLE_RUST(close);
            HANDLE_RUST(connect);
            HANDLE_RUST(creat);
            HANDLE_RUST(dup);
            HANDLE_RUST(dup2);
            HANDLE_RUST(dup3);
            HANDLE_RUST(epoll_create);
            HANDLE_RUST(epoll_create1);
            HANDLE_RUST(epoll_ctl);
            HANDLE_RUST(epoll_pwait);
            HANDLE_RUST(epoll_pwait2);
            HANDLE_RUST(epoll_wait);
            HANDLE_RUST(eventfd);
            HANDLE_RUST(eventfd2);
            HANDLE_RUST(execve);
            HANDLE_RUST(execveat);
            HANDLE_RUST(exit_group);
            HANDLE_RUST(faccessat);
            HANDLE_RUST(fadvise64);
            HANDLE_RUST(fallocate);
            HANDLE_RUST(fchmod);
            HANDLE_RUST(fchmodat);
            HANDLE_RUST(fchown);
            HANDLE_RUST(fchownat);
            HANDLE_RUST(fcntl);
            HANDLE_RUST(fdatasync);
            HANDLE_RUST(fgetxattr);
            HANDLE_RUST(flistxattr);
            HANDLE_RUST(flock);
            HANDLE_RUST(fork);
            HANDLE_RUST(fremovexattr);
            HANDLE_RUST(fsetxattr);
            HANDLE_RUST(fstat);
            HANDLE_RUST(fstatfs);
            HANDLE_RUST(fsync);
            HANDLE_RUST(ftruncate);
            HANDLE_RUST(futex);
            HANDLE_RUST(futimesat);
            HANDLE_RUST(getdents);
            HANDLE_RUST(getdents64);
            HANDLE_RUST(getitimer);
            HANDLE_RUST(getpeername);
            HANDLE_RUST(getpid);
            HANDLE_RUST(getpgrp);
            HANDLE_RUST(getpgid);
            HANDLE_RUST(getppid);
            HANDLE_RUST(getsid);
            HANDLE_RUST(gettid);
            HANDLE_RUST(getrandom);
            HANDLE_RUST(get_robust_list);
            HANDLE_RUST(getsockname);
            HANDLE_RUST(getsockopt);
            HANDLE_RUST(gettimeofday);
            HANDLE_RUST(ioctl);
            HANDLE_RUST(kill);
            HANDLE_RUST(linkat);
            HANDLE_RUST(listen);
            HANDLE_RUST(lseek);
            HANDLE_RUST(mkdirat);
            HANDLE_RUST(mknodat);
            HANDLE_RUST(mmap);
            HANDLE_RUST(mprotect);
            HANDLE_RUST(mremap);
            HANDLE_RUST(munmap);
            HANDLE_RUST(nanosleep);
            HANDLE_RUST(newfstatat);
            HANDLE_RUST(open);
            HANDLE_RUST(openat);
            HANDLE_RUST(pipe);
            HANDLE_RUST(pipe2);
            HANDLE_RUST(poll);
            HANDLE_RUST(ppoll);
            HANDLE_RUST(prctl);
            HANDLE_RUST(pread64);
            HANDLE_RUST(preadv);
#ifdef SYS_preadv2
            HANDLE_RUST(preadv2);
#endif
#ifdef SYS_prlimit64
            HANDLE_RUST(prlimit64);
#endif
            HANDLE_RUST(pselect6);
            HANDLE_RUST(pwrite64);
            HANDLE_RUST(pwritev);
#ifdef SYS_pwritev2
            HANDLE_RUST(pwritev2);
#endif
            HANDLE_RUST(read);
            HANDLE_RUST(readahead);
            HANDLE_RUST(readlinkat);
            HANDLE_RUST(readv);
            HANDLE_RUST(recvfrom);
            HANDLE_RUST(recvmsg);
            HANDLE_RUST(renameat);
            HANDLE_RUST(renameat2);
            HANDLE_RUST(rseq);
            HANDLE_RUST(sched_getaffinity);
            HANDLE_RUST(sched_setaffinity);
            HANDLE_RUST(sched_yield);
            HANDLE_RUST(shadow_hostname_to_addr_ipv4);
            HANDLE_RUST(shadow_init_memory_manager);
            HANDLE_RUST(shadow_yield);
            HANDLE_RUST(select);
            HANDLE_RUST(sendmsg);
            HANDLE_RUST(sendto);
            HANDLE_RUST(setpgid);
            HANDLE_RUST(setsid);
            HANDLE_RUST(setsockopt);
            HANDLE_RUST(rt_sigaction);
            HANDLE_RUST(sigaltstack);
            HANDLE_RUST(rt_sigprocmask);
            HANDLE_RUST(set_robust_list);
            HANDLE_RUST(setitimer);
            HANDLE_RUST(set_tid_address);
            HANDLE_RUST(shutdown);
            HANDLE_RUST(socket);
            HANDLE_RUST(socketpair);
#ifdef SYS_statx
            HANDLE_RUST(statx);
#endif
            HANDLE_RUST(symlinkat);
            HANDLE_RUST(sync_file_range);
            HANDLE_RUST(syncfs);
            HANDLE_RUST(sysinfo);
            HANDLE_RUST(tgkill);
            HANDLE_RUST(time);
            HANDLE_RUST(timerfd_create);
            HANDLE_RUST(timerfd_gettime);
            HANDLE_RUST(timerfd_settime);
            HANDLE_RUST(tkill);
            HANDLE_RUST(uname);
            HANDLE_RUST(unlinkat);
            HANDLE_RUST(utimensat);
            HANDLE_RUST(vfork);
            HANDLE_RUST(waitid);
            HANDLE_RUST(wait4);
            HANDLE_RUST(write);
            HANDLE_RUST(writev);

            // **************************************
            // Not handled (yet):
            // **************************************
            HANDLE_RUST(chdir);
            HANDLE_RUST(fchdir);

            HANDLE_RUST(io_getevents);
            HANDLE_RUST(msync);

            // copying data between various types of fds
            HANDLE_RUST(copy_file_range);
            HANDLE_RUST(sendfile);
            HANDLE_RUST(splice);
            HANDLE_RUST(vmsplice);
            HANDLE_RUST(tee);

            //// additional socket io
            HANDLE_RUST(recvmmsg);
            HANDLE_RUST(sendmmsg);

            // ***************************************
            // We think we don't need to handle these
            // (because the plugin can natively):
            // ***************************************
            HANDLE_RUST(access);
            HANDLE_RUST(arch_prctl);
            HANDLE_RUST(chmod);
            HANDLE_RUST(chown);
            HANDLE_RUST(exit);
            HANDLE_RUST(getcwd);
            HANDLE_RUST(geteuid);
            HANDLE_RUST(getegid);
            HANDLE_RUST(getgid);
            HANDLE_RUST(getgroups);
            HANDLE_RUST(getresgid);
            HANDLE_RUST(getresuid);
            HANDLE_RUST(getrlimit);
            HANDLE_RUST(getuid);
            HANDLE_RUST(getxattr);
            HANDLE_RUST(lchown);
            HANDLE_RUST(lgetxattr);
            HANDLE_RUST(link);
            HANDLE_RUST(listxattr);
            HANDLE_RUST(llistxattr);
            HANDLE_RUST(lremovexattr);
            HANDLE_RUST(lsetxattr);
            HANDLE_RUST(lstat);
            HANDLE_RUST(madvise);
            HANDLE_RUST(mkdir);
            HANDLE_RUST(mknod);
            HANDLE_RUST(readlink);
            HANDLE_RUST(removexattr);
            HANDLE_RUST(rename);
            HANDLE_RUST(rmdir);
            HANDLE_RUST(rt_sigreturn);
            HANDLE_RUST(setfsgid);
            HANDLE_RUST(setfsuid);
            HANDLE_RUST(setgid);
            HANDLE_RUST(setregid);
            HANDLE_RUST(setresgid);
            HANDLE_RUST(setresuid);
            HANDLE_RUST(setreuid);
            HANDLE_RUST(setrlimit);
            HANDLE_RUST(setuid);
            HANDLE_RUST(setxattr);
            HANDLE_RUST(stat);
            HANDLE_RUST(statfs);
            HANDLE_RUST(symlink);
            HANDLE_RUST(truncate);
            HANDLE_RUST(unlink);
            HANDLE_RUST(utime);
            HANDLE_RUST(utimes);

            default: {
                warning("Detected unsupported syscall %ld called from thread %i in process %s on "
                        "host %s",
                        args->number, sys->threadId, process_getPluginName(process),
                        host_getName(host));
                scr = syscallreturn_makeDoneI64(-ENOSYS);

                if (straceLoggingMode != STRACE_FMT_MODE_OFF) {
                    char arg_str[20] = {0};
                    snprintf(arg_str, sizeof(arg_str), "%ld, ...", args->number);
                    scr = log_syscall(process, straceLoggingMode, sys->threadId, "syscall", arg_str,
                                      &args->args, scr);
                }

                break;
            }
        }
    }

    // If the syscall would be blocked, but there's a signal pending, fail with
    // EINTR instead. The shim-side code will run the signal handlers and then
    // either return the EINTR or restart the syscall (See SA_RESTART in
    // signal(7)).
    //
    // We do this check *after* (not before) trying the syscall so that we don't
    // "interrupt" a syscall that wouldn't have blocked in the first place, or
    // that can return a "partial" result when interrupted. e.g. consider the
    // sequence:
    //
    // * Thread is blocked on reading a file descriptor.
    // * The read becomes ready and the thread is scheduled to run.
    // * The thread receives an unblocked signal.
    // * The thread runs again.
    //
    // In this scenario, the `read` call should be allowed to complete successfully.
    // from signal(7):  "If an I/O call on a slow device has already transferred
    // some data by the time it is interrupted by a signal handler, then the
    // call will return a success  status  (normally,  the  number of bytes
    // transferred)."
    if (scr.tag == SYSCALL_RETURN_BLOCK &&
        thread_unblockedSignalPending(thread, host_getShimShmemLock(host))) {
        SyscallReturnBlocked* blocked = syscallreturn_blocked(&scr);
        syscallcondition_unref(blocked->cond);
        scr = syscallreturn_makeInterrupted(blocked->restartable);
    }

    // Ensure pointers are flushed.
    if (!(scr.tag == SYSCALL_RETURN_DONE &&
          syscall_rawReturnValueToErrno(syscallreturn_done(&scr)->retval.as_i64) == 0)) {
        // The syscall didn't complete successfully; don't write back pointers.
        trace("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
        process_freePtrsWithoutFlushing(process);
    } else {
        int res = process_flushPtrs(process);
        if (res != 0) {
            panic("Flushing syscall ptrs: %s", g_strerror(-res));
        }
    }

    if (shimshmem_getModelUnblockedSyscallLatency(host_getSharedMem(host)) &&
        process_isRunning(process) &&
        (scr.tag == SYSCALL_RETURN_DONE || scr.tag == SYSCALL_RETURN_NATIVE)) {
        CSimulationTime maxUnappliedCpuLatency =
            shimshmem_maxUnappliedCpuLatency(host_getSharedMem(host));
        // Increment unblocked syscall latency, but only for
        // non-shadow-syscalls, since the latter are part of Shadow's
        // internal plumbing; they shouldn't necessarily "consume" time.
        if (!syscall_num_is_shadow(args->number)) {
            shimshmem_incrementUnappliedCpuLatency(
                host_getShimShmemLock(host),
                shimshmem_unblockedSyscallLatency(host_getSharedMem(host)));
        }
        const CSimulationTime unappliedCpuLatency =
            shimshmem_getUnappliedCpuLatency(host_getShimShmemLock(host));
        trace("Unapplied CPU latency amt=%ld max=%ld", unappliedCpuLatency, maxUnappliedCpuLatency);
        if (unappliedCpuLatency > maxUnappliedCpuLatency) {
            CEmulatedTime newTime = worker_getCurrentEmulatedTime() + unappliedCpuLatency;
            CEmulatedTime maxTime = worker_maxEventRunaheadTime(host);
            if (newTime <= maxTime) {
                trace("Reached unblocked syscall limit. Incrementing time");
                shimshmem_resetUnappliedCpuLatency(host_getShimShmemLock(host));
                worker_setCurrentEmulatedTime(newTime);
            } else {
                trace("Reached unblocked syscall limit. Yielding.");
                // Block instead, but save the result so that we can return it
                // later instead of re-executing the syscall.
                utility_debugAssert(!sys->havePendingResult);
                sys->havePendingResult = true;
                sys->pendingResult = scr;
                SysCallCondition* cond = syscallcondition_newWithAbsTimeout(newTime);

                scr = syscallreturn_makeBlocked(cond, false);
            }
        }
    }

    if (scr.tag == SYSCALL_RETURN_BLOCK) {
        /* We are blocking: store the syscall number so we know
         * to expect the same syscall again when it unblocks. */
        sys->blockedSyscallNR = args->number;
    } else {
        sys->blockedSyscallNR = -1;
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
