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

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/timer.h"
#include "main/host/process.h"
#include "main/host/syscall/epoll.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall/time.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

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
        /* Here we create the timer directly rather than going
         * through host_createDescriptor because the descriptor
         * is not being used to service a plugin syscall and it
         * should not be tracked with an fd handle. */
        .timer = timer_new(0, CLOCK_MONOTONIC, 0),
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
}

static void _syscallhandler_post_syscall(SysCallHandler* sys, long number,
                                         const char* name, SysCallReturn* scr) {
    debug("SYSCALL_HANDLER_POST(%s,pid=%u): syscall %ld %s result: state=%s%s "
          "code=%d",
          process_getPluginName(sys->process),
          process_getProcessID(sys->process), number, name,
          _syscallhandler_wasBlocked(sys) ? "BLOCK->" : "",
          scr->state == SYSCALL_DONE
              ? "DONE"
              : scr->state == SYSCALL_BLOCK
                    ? "BLOCK"
                    : scr->state == SYSCALL_NATIVE ? "NATIVE" : "UNKNOWN",
          (int)scr->retval.as_i64);
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
        HANDLE(clock_gettime);
        HANDLE(close);
        HANDLE(connect);
        HANDLE(epoll_create);
        HANDLE(epoll_create1);
        HANDLE(epoll_ctl);
        HANDLE(epoll_wait);
        HANDLE(getpeername);
        HANDLE(getpid);
        HANDLE(getsockname);
        HANDLE(listen);
        HANDLE(nanosleep);
        HANDLE(pipe);
        HANDLE(pipe2);
        HANDLE(read);
        HANDLE(recvfrom);
        HANDLE(sendto);
        HANDLE(shutdown);
        HANDLE(socket);
        HANDLE(uname);
        HANDLE(write);

        // **************************************
        // Needed for phold, but not handled yet:
        // **************************************
        // Test coverage: test/file
        NATIVE(fstat);
        // Test coverage: test/file (via open(3))
        NATIVE(openat);

        // **************************************
        // Needed for tor, but not handled yet:
        // **************************************
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(chown);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(clone);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(chmod);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(eventfd2);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(fallocate);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(fchmod);
        // Manipulate file descriptor.
        //
        // Called from tor(tor_fopen_cloexec) -> libpthread(__fcntl)
        NATIVE(fcntl);
#ifdef SYS_fcntl64
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(fcntl64);
#endif
        // Apply or remove advisory lock on an open file.
        //
        // Called from tor(tor_init) -> ... tor(set_options) -> ...
        // tor(tor_lockfile_lock)
        NATIVE(flock);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(fstatfs);
        // fast user-space locking. Basically handles blocking until a lock is
        // released, and conversely waking up blocked threads when it is
        // released.
        //
        // Called from CRYPTO_THREAD_run_once -> __pthread_once_slow
        NATIVE(futex);
        // get directory entries.
        // We'll need handle file descriptor remapping.
        NATIVE(getdents);
        // Called a few places in libcrypto.
        NATIVE(getrandom);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(getrlimit);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(getsockopt);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(gettimeofday);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(ioctl);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c.
        // It looks like it's configured to only work with signal=0; i.e.
        // check for process existence, but not actually send a signal.
        NATIVE(kill);
        NATIVE(lseek);
        NATIVE(mmap);
#ifdef SYS_mmap2
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(mmap2);
#endif
        // Only deals in address and offset, so might not need for correctness.
        // Might want it to GC any bookkeeping from corresponding mmap calls,
        // though.
        NATIVE(munmap);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(open);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(prctl);
        // Surprisingly, gets called while building the list of local
        // interfaces, using sa_family=AF_NETLINK.
        //
        // Called from tor(get_interface_addresses_raw) -> libc(getifaddrs) ->
        // libc(if_indextoname)
        NATIVE(recvmsg);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(setrlimit);
        // get/set list of robust futexes. The  purpose  of  the  robust futex
        // list is to ensure that if a thread accidentally fails to unlock a
        // futex before terminating or calling execve.
        //
        // Might be able to get away without emulating, but should take a closer
        // look when supporting threading, futex, etc.
        //
        // Called from pthread(__pthread_initialize_minimal)
        NATIVE(set_robust_list);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(setsockopt);
        // Set pointer to thread ID.
        //
        // Might be able to get away without emulating, but should take a closer
        // look when supporting threading, futex, etc.
        //
        // Called from pthread(__pthread_initialize_minimal)
        NATIVE(set_tid_address);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(socketpair);
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(time);

        // **************************************
        // Not handled (yet):
        // **************************************
        NATIVE(execve);

        // **************************************
        // Called from tor, but probably can continue to let plugin execute
        // natively.
        // **************************************
        // Checks if a file is accessible.
        NATIVE(access);
        // Change data segment size. Not needed for correctness, though might
        // be useful to intercept this later to put the heap in a pre-shared
        // memory region.
        NATIVE(brk);
        // Gets or sets segment registers.
        NATIVE(arch_prctl);
        // get real group id of calling process.
        NATIVE(getgid);
        // Returns effective user ID of the calling process.
        NATIVE(geteuid);
        // get effective group id of calling process.
        NATIVE(getegid);
        // get real user ID of calling process.
        NATIVE(getuid);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(lstat);
        // give advice to kernel about address range... to improve system or
        // application performance.
        NATIVE(madvise);
        // Found in strace of shadow-plugin-tor integration test.
        NATIVE(mkdir);
        // Change access protections of calling process's memory pages.
        NATIVE(mprotect);
        // Expands, shrinks, and/or moves a memory mapping.
        NATIVE(mremap);
        // Get and set resource limits for a specified process. Takes a pid, so
        // *may* need to remap if anything tries to set limits for other
        // processes. In the strace calls I see in Tor though, pid 0 is used,
        // meaning "this" process.
        //
        // Called from tor(set_max_file_descriptors)
        NATIVE(prlimit64);
        // Rename a file. Takes paths rather than file descriptors, so can
        // probably allow natively.
        //
        // Called from tor(save_state_callback) -> ... tor(finish_writing_to_file_impl)
        NATIVE(rename);
        // Change action taken by a process on receipt of a signal. Can probably
        // get away without this, assuming signals aren't used for non-error
        // cases.
        //
        // Called from:
        // - tor(handle_signals) -> ...
        NATIVE(rt_sigaction);
        // Fetch and/or change signal mask. As with rt_sigaction, we can
        // probably get away without it for now.
        NATIVE(rt_sigprocmask);
        // Deals in paths rather than file descriptors. Should be ok to allow
        // natively.
        NATIVE(stat);
#ifdef SYS_stat64
        // Whitelisted in tor/src/lib/sandbox/sandbox.c
        NATIVE(stat64);
#endif
        // Returns stats on memory, swap, load average.
        //
        // Might be needed for determinism, but the only call I see is actually
        // in libc's qsort.
        //
        // Called from libssl(SSL_rstate_string) -> libc(qsort_r)
        NATIVE(sysinfo);
        // unlinks (deletes) a file by name.
        NATIVE(unlink);
        default:
            info("unhandled syscall %ld", args->number);
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
