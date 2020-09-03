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

#include "main/bindings/c/bindings.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/clone.h"
#include "main/host/syscall/epoll.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/file.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/futex.h"
#include "main/host/syscall/ioctl.h"
#include "main/host/syscall/mman.h"
#include "main/host/syscall/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/random.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall/time.h"
#include "main/host/syscall/timerfd.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

// this is not defined on Ubuntu 16.04
#ifndef SYS_copy_file_range
#define SYS_copy_file_range 326
#endif

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
        // Used to track syscall handler performance
        .perfTimer = g_timer_new(),
    };

    MAGIC_INIT(sys);

    host_ref(host);
    process_ref(process);
    thread_ref(thread);

    worker_countObject(OBJECT_TYPE_SYSCALL_HANDLER, COUNTER_TYPE_NEW);
    return sys;
}

static void _syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    message("handled %li syscalls in %f seconds", sys->numSyscalls, sys->perfSecondsTotal);

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
    if (sys->perfTimer) {
        g_timer_destroy(sys->perfTimer);
    }

    MAGIC_CLEAR(sys);
    free(sys);
    worker_countObject(OBJECT_TYPE_SYSCALL_HANDLER, COUNTER_TYPE_FREE);
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
    debug("SYSCALL_HANDLER_PRE(%s,pid=%u): handling syscall %ld %s%s",
          process_getPluginName(sys->process),
          process_getProcessID(sys->process), number, name,
          _syscallhandler_wasBlocked(sys) ? " (previously BLOCKed)" : "");

    /* Track elapsed time during this syscall by marking the start time. */
    g_timer_start(sys->perfTimer);
}

static void _syscallhandler_post_syscall(SysCallHandler* sys, long number,
                                         const char* name, SysCallReturn* scr) {
    /* Add the cumulative elapsed seconds and num syscalls. */
    sys->perfSecondsCurrent += g_timer_elapsed(sys->perfTimer, NULL);

    debug("SYSCALL_HANDLER_POST(%s,pid=%u): syscall %ld %s result: state=%s%s "
          "code=%d(%s) in %f seconds",
          process_getPluginName(sys->process), process_getProcessID(sys->process), number, name,
          _syscallhandler_wasBlocked(sys) ? "BLOCK->" : "",
          scr->state == SYSCALL_DONE
              ? "DONE"
              : scr->state == SYSCALL_BLOCK ? "BLOCK"
                                            : scr->state == SYSCALL_NATIVE ? "NATIVE" : "UNKNOWN",
          (int)scr->retval.as_i64, strerror(-scr->retval.as_i64), sys->perfSecondsCurrent);

    if (scr->state != SYSCALL_BLOCK) {
        /* The syscall completed, count it and the cumulative time to complete it. */
        sys->numSyscalls++;
        sys->perfSecondsTotal += sys->perfSecondsCurrent;
        sys->perfSecondsCurrent = 0;
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
        debug("native syscall %ld " #s, args->number);                         \
        scr = (SysCallReturn){.state = SYSCALL_NATIVE};                        \
        break
SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    // Initialize the process's MemoryManager if it doesn't exist. In practice
    // this happens the first time a process makes a syscall, and on the first
    // syscall after an `exec` (which destroys the MemoryManager). It's done
    // here because the MemoryManager needs a plugin thread that's ready to
    // make syscalls in order to perform its initialization.
    if (!process_getMemoryManager(sys->process)) {
        process_setMemoryManager(sys->process, memorymanager_new(sys->thread));
    }
    SysCallReturn scr;

    /* Make sure that we either don't have a blocked syscall,
     * or if we blocked a syscall, then that same syscall
     * should be executed again when it becomes unblocked. */
    if (sys->blockedSyscallNR >= 0 && sys->blockedSyscallNR != args->number) {
        error("We blocked syscall number %ld but syscall number %ld "
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
        HANDLE(close);
        HANDLE(connect);
        HANDLE(creat);
        HANDLE(epoll_create);
        HANDLE(epoll_create1);
        HANDLE(epoll_ctl);
        HANDLE(epoll_wait);
        HANDLE(execve);
        HANDLE(faccessat);
        HANDLE(fadvise64);
        HANDLE(fallocate);
        HANDLE(fchdir);
        HANDLE(fchmod);
        HANDLE(fchmodat);
        HANDLE(fchown);
        HANDLE(fchownat);
        HANDLE(fcntl);
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
        HANDLE(gettid);
        HANDLE(getrandom);
        HANDLE(getsockname);
        HANDLE(getsockopt);
        HANDLE(gettimeofday);
        HANDLE(ioctl);
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
        HANDLE(pipe);
        HANDLE(pipe2);
        HANDLE(pread64);
        HANDLE(preadv);
#ifdef SYS_preadv2
        HANDLE(preadv2);
#endif
        HANDLE(pwrite64);
        HANDLE(pwritev);
#ifdef SYS_pwritev2
        HANDLE(pwritev2);
#endif
        HANDLE(read);
        HANDLE(readahead);
        HANDLE(readlinkat);
        HANDLE(readv);
        HANDLE(recvfrom);
        HANDLE(renameat);
        HANDLE(renameat2);
        HANDLE(sendto);
        HANDLE(setsockopt);
        HANDLE(shutdown);
        HANDLE(socket);
#ifdef SYS_statx
        HANDLE(statx);
#endif
        HANDLE(symlinkat);
        HANDLE(sync_file_range);
        HANDLE(syncfs);
        HANDLE(time);
        HANDLE(timerfd_create);
        HANDLE(timerfd_gettime);
        HANDLE(timerfd_settime);
        HANDLE(uname);
        HANDLE(unlinkat);
        HANDLE(utimensat);
        HANDLE(write);
        HANDLE(writev);

        // **************************************
        // Not handled (yet):
        // **************************************
        NATIVE(arch_prctl);
        NATIVE(eventfd2);
        NATIVE(io_getevents);
        NATIVE(prctl);
        NATIVE(prlimit64);
        NATIVE(rt_sigaction);
        NATIVE(rt_sigprocmask);
        NATIVE(get_robust_list);
        NATIVE(set_robust_list);
        NATIVE(set_tid_address);
        NATIVE(sysinfo);
        NATIVE(waitid);

        // operations on pids (shadow overrides pids)
        NATIVE(kill);
        NATIVE(tkill);
        NATIVE(tgkill);
        NATIVE(sched_getaffinity);
        NATIVE(sched_setaffinity);

        // operations on file descriptors
        NATIVE(dup);
        NATIVE(dup2);
        NATIVE(dup3);
#ifdef SYS_fcntl64
        NATIVE(fcntl64);
#endif
        NATIVE(poll);
        NATIVE(ppoll);
        NATIVE(select);
        NATIVE(pselect6);

        // copying data between various types of fds
        NATIVE(copy_file_range);
        NATIVE(sendfile);
        NATIVE(splice);
        NATIVE(vmsplice);
        NATIVE(tee);

        // additional socket io
        NATIVE(recvmsg);
        NATIVE(sendmsg);
        NATIVE(recvmmsg);
        NATIVE(sendmmsg);

        // ***************************************
        // We think we don't need to handle these
        // (because the plugin can natively):
        // ***************************************
        NATIVE(access);
        NATIVE(exit);
        NATIVE(exit_group);
        NATIVE(getcwd);
        NATIVE(geteuid);
        NATIVE(getegid);
        NATIVE(getgid);
        NATIVE(getrlimit);
        NATIVE(getuid);
        NATIVE(lstat);
        NATIVE(madvise);
        NATIVE(mkdir);
        NATIVE(readlink);
        NATIVE(setrlimit);
        NATIVE(stat);
#ifdef SYS_stat64
        NATIVE(stat64);
#endif
        NATIVE(statfs);
        NATIVE(unlink);

        default:
            warning("unhandled syscall %ld", args->number);
            scr = (SysCallReturn){.state = SYSCALL_NATIVE};
            break;
    }

    if (scr.state == SYSCALL_BLOCK) {
        /* We are blocking: store the syscall number so we know
         * to expect the same syscall again when it unblocks. */
        sys->blockedSyscallNR = args->number;
    } else if (_syscallhandler_wasBlocked(sys)) {
        /* We were but are no longer blocked on a syscall. Make
         * sure any previously used listener timeouts are ignored.*/
        _syscallhandler_setListenTimeout(sys, NULL);
        sys->blockedSyscallNR = -1;
    }

    return scr;
}
#undef NATIVE
#undef HANDLE
