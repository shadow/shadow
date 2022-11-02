/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_event.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/clone.h"
#include "main/host/syscall/epoll.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/file.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/futex.h"
#include "main/host/syscall/mman.h"
#include "main/host/syscall/poll.h"
#include "main/host/syscall/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/select.h"
#include "main/host/syscall/shadow.h"
#include "main/host/syscall/signal.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall/time.h"
#include "main/host/syscall/timerfd.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_numbers.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/utility/syscall.h"

// Not defined in some older libc's.
#ifndef SYS_rseq
#define SYS_rseq 334
#endif

static bool _countSyscalls = false;
ADD_CONFIG_HANDLER(config_getUseSyscallCounters, _countSyscalls)

const Host* _syscallhandler_getHost(const SysCallHandler* sys) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == sys->hostId);
    return host;
}

SysCallHandler* syscallhandler_new(const Host* host, Process* process, Thread* thread) {
    utility_debugAssert(host);
    utility_debugAssert(process);
    utility_debugAssert(thread);

    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .hostId = host_getID(host),
        .process = process,
        .thread = thread,
        .syscall_handler_rs = rustsyscallhandler_new(),
        .blockedSyscallNR = -1,
        .referenceCount = 1,
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

    process_ref(process);
    thread_ref(thread);

    worker_count_allocation(SysCallHandler);
    return sys;
}

static void _syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

#ifdef USE_PERF_TIMERS
    info("handled %li syscalls in %f seconds", sys->numSyscalls, sys->perfSecondsTotal);
#else
    info("handled %li syscalls", sys->numSyscalls);
#endif

    if (_countSyscalls && sys->syscall_counter) {
        // Log the plugin thread specific counts
        char* str = counter_alloc_string(sys->syscall_counter);
        info("Thread %d (%s) syscall counts: %s", thread_getID(sys->thread),
             process_getPluginName(sys->process), str);
        counter_free_string(sys->syscall_counter, str);

        // Add up the counts at the worker level
        worker_add_syscall_counts(sys->syscall_counter);

        // Cleanup
        counter_free(sys->syscall_counter);
    }

    if (sys->process) {
        process_unref(sys->process);
    }
    if (sys->thread) {
        thread_unref(sys->thread);
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

void syscallhandler_ref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)++;
}

void syscallhandler_unref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)--;
    utility_debugAssert(sys->referenceCount >= 0);
    if(sys->referenceCount == 0) {
        _syscallhandler_free(sys);
    }
}

static void _syscallhandler_pre_syscall(SysCallHandler* sys, long number,
                                        const char* name) {
    trace("SYSCALL_HANDLER_PRE(%s,pid=%u): handling syscall %ld %s%s",
          process_getPluginName(sys->process),
          thread_getID(sys->thread), number, name,
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

static void _syscallhandler_post_syscall(SysCallHandler* sys, long number,
                                         const char* name, SysCallReturn* scr) {
#ifdef USE_PERF_TIMERS
    /* Add the cumulative elapsed seconds and num syscalls. */
    sys->perfSecondsCurrent += g_timer_elapsed(sys->perfTimer, NULL);
#endif
    if (logger_isEnabled(logger_getDefault(), LOGLEVEL_TRACE)) {
        const char* errstr = "n/a";
        char errstrbuf[100];

        const char* valstr = "n/a";
        char valbuf[100];
        if (scr->state == SYSCALL_DONE) {
            SysCallReturnDone* done = syscallreturn_done(scr);
            if (done->retval.as_i64 < 0) {
                errstr = strerror_r(-done->retval.as_i64, errstrbuf, sizeof(errstrbuf));
            }
            snprintf(valbuf, sizeof(valbuf), "%" PRIi64, done->retval.as_i64);
            valstr = valbuf;
        }
        trace("SYSCALL_HANDLER_POST(%s,pid=%u): syscall %ld %s result: state=%s%s "
              "val=%s(%s)",
              process_getPluginName(sys->process), thread_getID(sys->thread), number, name,
              _syscallhandler_wasBlocked(sys) ? "BLOCK->" : "", syscallreturnstate_str(scr->state),
              valstr, errstr);
    }

#ifdef USE_PERF_TIMERS
    debug("handling syscall %ld %s took %f seconds", number, name, sys->perfSecondsCurrent);
#endif

    if (scr->state != SYSCALL_BLOCK) {
        /* The syscall completed, count it and the cumulative time to complete it. */
        sys->numSyscalls++;
#ifdef USE_PERF_TIMERS
        sys->perfSecondsTotal += sys->perfSecondsCurrent;
        sys->perfSecondsCurrent = 0;
#endif
    }
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

#define HANDLE_C(s)                                                                                \
    case SYS_##s:                                                                                  \
        _syscallhandler_pre_syscall(sys, args->number, #s);                                        \
        scr = syscallhandler_##s(sys, args);                                                       \
        if (straceLoggingMode != STRACE_FMT_MODE_OFF) {                                            \
            scr = log_syscall(                                                                     \
                sys->process, straceLoggingMode, thread_getID(sys->thread), #s, "...", scr);       \
        }                                                                                          \
        _syscallhandler_post_syscall(sys, args->number, #s, &scr);                                 \
        break
#define NATIVE(s)                                                                                  \
    case SYS_##s:                                                                                  \
        trace("native syscall %ld " #s, args->number);                                             \
        scr = syscallreturn_makeNative();                                                          \
        if (straceLoggingMode != STRACE_FMT_MODE_OFF) {                                            \
            scr = log_syscall(                                                                     \
                sys->process, straceLoggingMode, thread_getID(sys->thread), #s, "...", scr);       \
        }                                                                                          \
        break
#define UNSUPPORTED(s)                                                                             \
    case SYS_##s:                                                                                  \
        error("Returning error ENOSYS for explicitly unsupported syscall %ld " #s, args->number);  \
        scr = syscallreturn_makeDoneErrno(ENOSYS);                                                 \
        if (straceLoggingMode != STRACE_FMT_MODE_OFF) {                                            \
            scr = log_syscall(                                                                     \
                sys->process, straceLoggingMode, thread_getID(sys->thread), #s, "...", scr);       \
        }                                                                                          \
        break
#define HANDLE_RUST(s)                                                                             \
    case SYS_##s: {                                                                                \
        _syscallhandler_pre_syscall(sys, args->number, #s);                                        \
        SyscallHandler* handler = sys->syscall_handler_rs;                                         \
        sys->syscall_handler_rs = NULL;                                                            \
        scr = rustsyscallhandler_syscall(handler, sys, args);                                      \
        sys->syscall_handler_rs = handler;                                                         \
        _syscallhandler_post_syscall(sys, args->number, #s, &scr);                                 \
    } break

SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    StraceFmtMode straceLoggingMode = process_straceLoggingMode(sys->process);
    const Host* host = _syscallhandler_getHost(sys);

    SysCallReturn scr;

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
        utility_debugAssert(sys->pendingResult.state != SYSCALL_BLOCK);
        sys->blockedSyscallNR = -1;
        return sys->pendingResult;
    } else {
        switch (args->number) {
            HANDLE_RUST(accept);
            HANDLE_RUST(accept4);
            HANDLE_RUST(bind);
            HANDLE_C(brk);
            HANDLE_C(clock_gettime);
            HANDLE_C(clock_nanosleep);
            HANDLE_C(clone);
            HANDLE_RUST(close);
            HANDLE_RUST(connect);
            HANDLE_C(creat);
            HANDLE_RUST(dup);
            HANDLE_RUST(dup2);
            HANDLE_RUST(dup3);
            HANDLE_C(epoll_create);
            HANDLE_C(epoll_create1);
            HANDLE_C(epoll_ctl);
            HANDLE_C(epoll_pwait);
            HANDLE_C(epoll_wait);
            HANDLE_RUST(eventfd);
            HANDLE_RUST(eventfd2);
            HANDLE_C(execve);
            HANDLE_C(exit_group);
            HANDLE_C(faccessat);
            HANDLE_C(fadvise64);
            HANDLE_C(fallocate);
            HANDLE_C(fchmod);
            HANDLE_C(fchmodat);
            HANDLE_C(fchown);
            HANDLE_C(fchownat);
            HANDLE_RUST(fcntl);
#ifdef SYS_fcntl64
            // TODO: is there a nicer way to do this? Rust libc::SYS_fcntl64 does not exist.
            case SYS_fcntl64: {
                _syscallhandler_pre_syscall(sys, args->number, "fcntl64");
                SyscallHandler* handler = sys->syscall_handler_rs;
                sys->syscall_handler_rs = NULL;
                args->number = SYS_fcntl;
                scr = rustsyscallhandler_syscall(handler, sys, args);
                args->number = SYS_fcntl64;
                sys->syscall_handler_rs = handler;
                _syscallhandler_post_syscall(sys, args->number, "fcntl64", &scr);
                break;
            }
#endif
            HANDLE_C(fdatasync);
            HANDLE_C(fgetxattr);
            HANDLE_C(flistxattr);
            HANDLE_C(flock);
            HANDLE_C(fremovexattr);
            HANDLE_C(fsetxattr);
            HANDLE_C(fstat);
            HANDLE_C(fstatfs);
            HANDLE_C(fsync);
            HANDLE_C(ftruncate);
            HANDLE_C(futex);
            HANDLE_C(futimesat);
            HANDLE_C(getdents);
            HANDLE_C(getdents64);
            HANDLE_RUST(getitimer);
            HANDLE_RUST(getpeername);
            HANDLE_C(getpid);
            HANDLE_C(getppid);
            HANDLE_C(gettid);
            HANDLE_RUST(getrandom);
            HANDLE_C(get_robust_list);
            HANDLE_RUST(getsockname);
            HANDLE_RUST(getsockopt);
            HANDLE_C(gettimeofday);
            HANDLE_RUST(ioctl);
            HANDLE_C(kill);
            HANDLE_C(linkat);
            HANDLE_RUST(listen);
            HANDLE_C(lseek);
            HANDLE_C(mkdirat);
            HANDLE_C(mknodat);
            HANDLE_C(mmap);
#ifdef SYS_mmap2
            HANDLE_C(mmap2);
#endif
            HANDLE_C(mprotect);
            HANDLE_C(mremap);
            HANDLE_C(munmap);
            HANDLE_C(nanosleep);
            HANDLE_C(newfstatat);
            HANDLE_RUST(open);
            HANDLE_RUST(openat);
            HANDLE_RUST(pipe);
            HANDLE_RUST(pipe2);
            HANDLE_C(poll);
            HANDLE_C(ppoll);
            HANDLE_C(prctl);
            HANDLE_RUST(pread64);
            HANDLE_C(preadv);
#ifdef SYS_preadv2
            HANDLE_C(preadv2);
#endif
#ifdef SYS_prlimit
            HANDLE_C(prlimit);
#endif
#ifdef SYS_prlimit64
            HANDLE_C(prlimit64);
#endif
            HANDLE_C(pselect6);
            HANDLE_RUST(pwrite64);
            HANDLE_C(pwritev);
#ifdef SYS_pwritev2
            HANDLE_C(pwritev2);
#endif
            HANDLE_RUST(read);
            HANDLE_C(readahead);
            HANDLE_C(readlinkat);
            HANDLE_C(readv);
            HANDLE_RUST(recvfrom);
            HANDLE_C(renameat);
            HANDLE_C(renameat2);
            HANDLE_RUST(rseq);
            HANDLE_RUST(sched_yield);
            HANDLE_C(shadow_get_ipc_blk);
            HANDLE_C(shadow_get_shm_blk);
            HANDLE_C(shadow_hostname_to_addr_ipv4);
            HANDLE_C(shadow_init_memory_manager);
            HANDLE_C(shadow_yield);
            HANDLE_C(select);
            HANDLE_RUST(sendto);
            HANDLE_RUST(setsockopt);
#ifdef SYS_sigaction
            // Superseded by rt_sigaction in Linux 2.2
            UNSUPPORTED(sigaction);
#endif
            HANDLE_C(rt_sigaction);
            HANDLE_C(sigaltstack);
#ifdef SYS_signal
            // Superseded by sigaction in glibc 2.0
            UNSUPPORTED(signal);
#endif
#ifdef SYS_sigprocmask
            // Superseded by rt_sigprocmask in Linux 2.2
            UNSUPPORTED(sigprocmask);
#endif
            HANDLE_C(rt_sigprocmask);
            HANDLE_C(set_robust_list);
            HANDLE_RUST(setitimer);
            HANDLE_C(set_tid_address);
            HANDLE_RUST(shutdown);
            HANDLE_RUST(socket);
            HANDLE_RUST(socketpair);
#ifdef SYS_statx
            HANDLE_C(statx);
#endif
            HANDLE_C(symlinkat);
            HANDLE_C(sync_file_range);
            HANDLE_C(syncfs);
            HANDLE_RUST(sysinfo);
            HANDLE_C(tgkill);
            HANDLE_C(time);
            HANDLE_C(timerfd_create);
            HANDLE_C(timerfd_gettime);
            HANDLE_C(timerfd_settime);
            HANDLE_C(tkill);
            HANDLE_C(uname);
            HANDLE_C(unlinkat);
            HANDLE_C(utimensat);
            HANDLE_RUST(write);
            HANDLE_C(writev);

            // **************************************
            // Not handled (yet):
            // **************************************
            // NATIVE(chdir);
            // NATIVE(fchdir);
            // NATIVE(io_getevents);
            // NATIVE(waitid);
            // NATIVE(msync);

            //// operations on pids (shadow overrides pids)
            // NATIVE(sched_getaffinity);
            // NATIVE(sched_setaffinity);

            //// copying data between various types of fds
            // NATIVE(copy_file_range);
            // NATIVE(sendfile);
            // NATIVE(splice);
            // NATIVE(vmsplice);
            // NATIVE(tee);

            //// additional socket io
            // NATIVE(recvmsg);
            // NATIVE(sendmsg);
            // NATIVE(recvmmsg);
            // NATIVE(sendmmsg);

            // ***************************************
            // We think we don't need to handle these
            // (because the plugin can natively):
            // ***************************************
            NATIVE(access);
            NATIVE(arch_prctl);
            NATIVE(chmod);
            NATIVE(chown);
            NATIVE(exit);
            NATIVE(getcwd);
            NATIVE(geteuid);
            NATIVE(getegid);
            NATIVE(getgid);
            NATIVE(getgroups);
            NATIVE(getresgid);
            NATIVE(getresuid);
            NATIVE(getrlimit);
            NATIVE(getuid);
            NATIVE(getxattr);
            NATIVE(lchown);
            NATIVE(lgetxattr);
            NATIVE(link);
            NATIVE(listxattr);
            NATIVE(llistxattr);
            NATIVE(lremovexattr);
            NATIVE(lsetxattr);
            NATIVE(lstat);
            NATIVE(madvise);
            NATIVE(mkdir);
            NATIVE(mknod);
            NATIVE(readlink);
            NATIVE(removexattr);
            NATIVE(rename);
            NATIVE(rmdir);
            NATIVE(rt_sigreturn);
            NATIVE(setfsgid);
            NATIVE(setfsuid);
            NATIVE(setgid);
            NATIVE(setregid);
            NATIVE(setresgid);
            NATIVE(setresuid);
            NATIVE(setreuid);
            NATIVE(setrlimit);
            NATIVE(setuid);
            NATIVE(setxattr);
            NATIVE(stat);
#ifdef SYS_stat64
            NATIVE(stat64);
#endif
            NATIVE(statfs);
            NATIVE(symlink);
            NATIVE(truncate);
            NATIVE(unlink);
            NATIVE(utime);
            NATIVE(utimes);

            // ***************************************
            // Syscalls that aren't implemented yet. Listing them here gives the same behavior
            // as the default case (returning ENOSYS), but allows the logging to include the
            // syscall name instead of just the number.
            // ***************************************

            UNSUPPORTED(sched_getaffinity);
            UNSUPPORTED(sched_setaffinity);

            default: {
                warning("Detected unsupported syscall %ld called from thread %i in process %s on "
                        "host %s",
                        args->number, thread_getID(sys->thread), process_getName(sys->process),
                        host_getName(host));
                error("Returning error %i (ENOSYS) for unsupported syscall %li, which may result in "
                      "unusual behavior",
                      ENOSYS, args->number);
                scr = syscallreturn_makeDoneI64(-ENOSYS);

                if (straceLoggingMode != STRACE_FMT_MODE_OFF) {
                    char arg_str[20] = {0};
                    snprintf(arg_str, sizeof(arg_str), "%ld, ...", args->number);
                    scr = log_syscall(sys->process, straceLoggingMode, thread_getID(sys->thread),
                                      "syscall", arg_str, scr);
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
    if (scr.state == SYSCALL_BLOCK &&
        thread_unblockedSignalPending(sys->thread, host_getShimShmemLock(host))) {
        SysCallReturnBlocked* blocked = syscallreturn_blocked(&scr);
        syscallcondition_unref(blocked->cond);
        scr = syscallreturn_makeInterrupted(blocked->restartable);
    }

    if (!(scr.state == SYSCALL_DONE &&
          syscall_rawReturnValueToErrno(syscallreturn_done(&scr)->retval.as_i64) == 0)) {
        // The syscall didn't complete successfully; don't write back pointers.
        trace("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
        process_freePtrsWithoutFlushing(sys->process);
    }

    if (shimshmem_getModelUnblockedSyscallLatency(host_getSharedMem(host)) &&
        process_isRunning(sys->process) &&
        (scr.state == SYSCALL_DONE || scr.state == SYSCALL_NATIVE)) {
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
                SysCallCondition* cond = syscallcondition_new((Trigger){.type = TRIGGER_NONE});
                syscallcondition_setTimeout(cond, host, newTime);
                scr = syscallreturn_makeBlocked(cond, false);
            }
        }
    }

    if (scr.state == SYSCALL_BLOCK) {
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
