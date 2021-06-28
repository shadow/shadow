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
#include "lib/shim/shim_event.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/clone.h"
#include "main/host/syscall/epoll.h"
#include "main/host/syscall/eventfd.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/file.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/futex.h"
#include "main/host/syscall/ioctl.h"
#include "main/host/syscall/mman.h"
#include "main/host/syscall/poll.h"
#include "main/host/syscall/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/random.h"
#include "main/host/syscall/shadow.h"
#include "main/host/syscall/signal.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall/sysinfo.h"
#include "main/host/syscall/time.h"
#include "main/host/syscall/timerfd.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_numbers.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/utility/syscall.h"

static bool _useMM = true;
ADD_CONFIG_HANDLER(config_getUseMemoryManager, _useMM)

static bool _countSyscalls = false;
ADD_CONFIG_HANDLER(config_getUseSyscallCounters, _countSyscalls)

SysCallHandler* syscallhandler_new(Host* host, Process* process,
                                   Thread* thread) {
    utility_assert(host);
    utility_assert(process);
    utility_assert(thread);

    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .host = host,
        .process = process,
        .thread = thread,
        .blockedSyscallNR = -1,
        .referenceCount = 1,
        /* Here we create the timer directly and do not register
         * with the process descriptor table because the descriptor
         * is not being used to service a plugin syscall and it
         * should not be tracked with an fd handle. */
        .timer = timer_new(),
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

    host_ref(host);
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

    if (sys->host) {
        host_unref(sys->host);
    }
    if (sys->process) {
        process_unref(sys->process);
    }
    if (sys->thread) {
        thread_unref(sys->thread);
    }

    if (sys->timer) {
        descriptor_unref(sys->timer);
    }
    if (sys->epoll) {
        descriptor_unref(sys->epoll);
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
    utility_assert(sys->referenceCount >= 0);
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

    trace("SYSCALL_HANDLER_POST(%s,pid=%u): syscall %ld %s result: state=%s%s "
          "code=%d(%s)",
          process_getPluginName(sys->process), thread_getID(sys->thread), number, name,
          _syscallhandler_wasBlocked(sys) ? "BLOCK->" : "",
          scr->state == SYSCALL_DONE
              ? "DONE"
              : scr->state == SYSCALL_BLOCK ? "BLOCK"
                                            : scr->state == SYSCALL_NATIVE ? "NATIVE" : "UNKNOWN",
          (int)scr->retval.as_i64, scr->retval.as_i64 < 0 ? strerror(-scr->retval.as_i64) : "n/a");

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

#define HANDLE(s)                                                              \
    case SYS_##s:                                                              \
        _syscallhandler_pre_syscall(sys, args->number, #s);                    \
        scr = syscallhandler_##s(sys, args);                                   \
        _syscallhandler_post_syscall(sys, args->number, #s, &scr);             \
        break
#define NATIVE(s)                                                              \
    case SYS_##s:                                                              \
        trace("native syscall %ld " #s, args->number);                         \
        scr = (SysCallReturn){.state = SYSCALL_NATIVE};                        \
        break
#define UNSUPPORTED(s)                                                                             \
    case SYS_##s:                                                                                  \
        error("Returning error ENOSYS for explicitly unsupported syscall %ld " #s, args->number);  \
        scr = (SysCallReturn){.state = -ENOSYS};                                                   \
        break

#ifdef USE_C_SYSCALLS
#define HANDLE_RUST(s) HANDLE(s)
#else
#define HANDLE_RUST(s)                                                         \
    case SYS_##s:                                                              \
        _syscallhandler_pre_syscall(sys, args->number, #s);                    \
        scr = rustsyscallhandler_##s(sys, args);                               \
        _syscallhandler_post_syscall(sys, args->number, #s, &scr);             \
        break
#endif

SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    // Initialize the process's MemoryManager if it doesn't exist. In practice
    // this happens the first time a process makes a syscall, and on the first
    // syscall after an `exec` (which destroys the MemoryManager). It's done
    // here because the MemoryManager needs a plugin thread that's ready to
    // make syscalls in order to perform its initialization.
    if (_useMM) {
        memorymanager_initMapperIfNeeded(process_getMemoryManager(sys->process), sys->thread);
    }
    SysCallReturn scr;

    /* Make sure that we either don't have a blocked syscall,
     * or if we blocked a syscall, then that same syscall
     * should be executed again when it becomes unblocked. */
    if (sys->blockedSyscallNR >= 0 && sys->blockedSyscallNR != args->number) {
        utility_panic("We blocked syscall number %ld but syscall number %ld "
                      "is unexpectedly being invoked",
                      sys->blockedSyscallNR, args->number);
    }

    switch (args->number) {
        HANDLE(accept);
        HANDLE(accept4);
        HANDLE(bind);
        HANDLE(brk);
        HANDLE(clock_gettime);
        HANDLE(clone);
        HANDLE_RUST(close);
        HANDLE(connect);
        HANDLE(creat);
        HANDLE_RUST(dup);
        HANDLE(epoll_create);
        HANDLE(epoll_create1);
        HANDLE(epoll_ctl);
        HANDLE(epoll_wait);
        HANDLE(eventfd);
        HANDLE(eventfd2);
        HANDLE(execve);
        HANDLE(exit_group);
        HANDLE(faccessat);
        HANDLE(fadvise64);
        HANDLE(fallocate);
        HANDLE(fchmod);
        HANDLE(fchmodat);
        HANDLE(fchown);
        HANDLE(fchownat);
        HANDLE(fcntl);
#ifdef SYS_fcntl64
        HANDLE(fcntl64);
#endif
        HANDLE(fdatasync);
        HANDLE(fgetxattr);
        HANDLE(flistxattr);
        HANDLE(flock);
        HANDLE(fremovexattr);
        HANDLE(fsetxattr);
        HANDLE(fstat);
        HANDLE(fstatfs);
        HANDLE(fsync);
        HANDLE(ftruncate);
        HANDLE(futex);
        HANDLE(futimesat);
        HANDLE(getdents);
        HANDLE(getdents64);
        HANDLE(getpeername);
        HANDLE(getpid);
        HANDLE(getppid);
        HANDLE(gettid);
        HANDLE(getrandom);
        HANDLE(get_robust_list);
        HANDLE(getsockname);
        HANDLE(getsockopt);
        HANDLE(gettimeofday);
        HANDLE(ioctl);
        HANDLE(kill);
        HANDLE(linkat);
        HANDLE(listen);
        HANDLE(lseek);
        HANDLE(mkdirat);
        HANDLE(mknodat);
        HANDLE(mmap);
#ifdef SYS_mmap2
        HANDLE(mmap2);
#endif
        HANDLE(mprotect);
        HANDLE(mremap);
        HANDLE(munmap);
        HANDLE(nanosleep);
        HANDLE(newfstatat);
        HANDLE(open);
        HANDLE(openat);
        HANDLE_RUST(pipe);
        HANDLE_RUST(pipe2);
        HANDLE(poll);
        HANDLE(ppoll);
        HANDLE(prctl);
        HANDLE_RUST(pread64);
        HANDLE(preadv);
#ifdef SYS_preadv2
        HANDLE(preadv2);
#endif
#ifdef SYS_prlimit
        HANDLE(prlimit);
#endif
#ifdef SYS_prlimit64
        HANDLE(prlimit64);
#endif
        HANDLE_RUST(pwrite64);
        HANDLE(pwritev);
#ifdef SYS_pwritev2
        HANDLE(pwritev2);
#endif
        HANDLE_RUST(read);
        HANDLE(readahead);
        HANDLE(readlinkat);
        HANDLE(readv);
        HANDLE(recvfrom);
        HANDLE(renameat);
        HANDLE(renameat2);
        HANDLE(shadow_set_ptrace_allow_native_syscalls);
        HANDLE(shadow_get_ipc_blk);
        HANDLE(shadow_get_shm_blk);
        HANDLE(shadow_hostname_to_addr_ipv4);
        HANDLE(sendto);
        HANDLE(setsockopt);
#ifdef SYS_sigaction
        // Superseded by rt_sigaction in Linux 2.2
        UNSUPPORTED(sigaction);
#endif
        HANDLE(rt_sigaction);
#ifdef SYS_signal
        // Superseded by sigaction in glibc 2.0
        UNSUPPORTED(signal);
#endif
#ifdef SYS_sigprocmask
        // Superseded by rt_sigprocmask in Linux 2.2
        UNSUPPORTED(sigprocmask);
#endif
        HANDLE(rt_sigprocmask);
        HANDLE(set_robust_list);
        HANDLE(set_tid_address);
        HANDLE(shutdown);
        HANDLE(socket);
        HANDLE(socketpair);
#ifdef SYS_statx
        HANDLE(statx);
#endif
        HANDLE(symlinkat);
        HANDLE(sync_file_range);
        HANDLE(syncfs);
        HANDLE(sysinfo);
        HANDLE(tgkill);
        HANDLE(time);
        HANDLE(timerfd_create);
        HANDLE(timerfd_gettime);
        HANDLE(timerfd_settime);
        HANDLE(tkill);
        HANDLE(uname);
        HANDLE(unlinkat);
        HANDLE(utimensat);
        HANDLE_RUST(write);
        HANDLE(writev);

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

        //// operations on file descriptors
        // NATIVE(dup2);
        // NATIVE(dup3);
        // NATIVE(select);
        // NATIVE(pselect6);

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
        NATIVE(sigaltstack);
        NATIVE(symlink);
        NATIVE(truncate);
        NATIVE(unlink);
        NATIVE(utime);
        NATIVE(utimes);

        default:
            warning(
                "Detected unsupported syscall %ld called from thread %i in process %s on host %s",
                args->number, thread_getID(sys->thread), process_getName(sys->process),
                host_getName(sys->host));
            error("Returning error %i (ENOSYS) for unsupported syscall %li, which may result in "
                  "unusual behavior",
                  ENOSYS, args->number);
            scr = (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOSYS};
            break;
    }

    if (scr.state == SYSCALL_BLOCK) {
        /* We are blocking: store the syscall number so we know
         * to expect the same syscall again when it unblocks. */
        sys->blockedSyscallNR = args->number;
    } else if (_syscallhandler_wasBlocked(sys)) {
        /* We were but are no longer blocked on a syscall. Make
         * sure any previously used listener timeouts are ignored.*/
        _syscallhandler_setListenTimeout(sys, NULL, TIMEOUT_RELATIVE);
        sys->blockedSyscallNR = -1;
    }

    if (!(scr.state == SYSCALL_DONE && syscall_rawReturnValueToErrno(scr.retval.as_i64) == 0)) {
        // The syscall didn't complete successfully; don't write back pointers.
        trace("Syscall didn't complete successfully; discarding plugin ptrs without writing back.");
        process_freePtrsWithoutFlushing(sys->process);
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
