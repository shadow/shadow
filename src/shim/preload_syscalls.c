// Defines system call wrappers: functions that are
// documented in man section 2. (See `man man`).

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <unistd.h>

#include "main/host/syscall/kernel_types.h"
#include "main/shmem/shmem_allocator.h"
#include "shim/ipc.h"
#include "shim/shim.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "shim/shim_shmem.h"
#include "support/logger/logger.h"

static long shadow_retval_to_errno(long retval) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (retval <= -1 && retval >= -4095) {
        errno = -retval;
        return -1;
    }
    return retval;
}

static long _vreal_syscall(long n, va_list args) {
    long arg1 = va_arg(args, long);
    long arg2 = va_arg(args, long);
    long arg3 = va_arg(args, long);
    long arg4 = va_arg(args, long);
    long arg5 = va_arg(args, long);
    long arg6 = va_arg(args, long);
    long rv;

    // r8, r9, and r10 aren't supported as register-constraints in
    // extended asm templates. We have to use [local register
    // variables](https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html)
    // instead. Calling any functions in between the register assignment and the
    // asm template could clobber these registers, which is why we don't do the
    // assignment directly above.
    register long r10 __asm__("r10") = arg4;
    register long r8 __asm__("r8") = arg5;
    register long r9 __asm__("r9") = arg6;
    __asm__ __volatile__(
        "syscall"
        : "=a"(rv)
        : "a"(n), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return shadow_retval_to_errno(rv);
}

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
static long _real_syscall(long n, ...) {
    va_list args;
    va_start(args, n);
    long rv = _vreal_syscall(n, args);
    va_end(args);
    return rv;
}

static SysCallReg _shadow_syscall_event(const ShimEvent* syscall_event) {

    ShMemBlock ipc_blk = shim_thisThreadEventIPCBlk();

    debug("sending syscall %ld event on %p",
          syscall_event->event_data.syscall.syscall_args.number, ipc_blk.p);

    shimevent_sendEventToShadow(ipc_blk.p, syscall_event);
    SysCallReg rv = {0};

    // By default we assume Shadow will return quickly, and so should spin
    // rather than letting the OS block this thread.
    bool spin = true;
    while (true) {
        debug("waiting for event on %p", ipc_blk.p);
        ShimEvent res = {0};
        shimevent_recvEventFromShadow(ipc_blk.p, &res, spin);
        debug("got response of type %d on %p", res.event_id, ipc_blk.p);
        // Reset spin-flag to true. (May have been set to false by a SHD_SHIM_EVENT_BLOCK in the
        // previous iteration)
        spin = true;
        switch (res.event_id) {
            case SHD_SHIM_EVENT_BLOCK: {
                // Loop again, this time relinquishing the CPU while waiting for the next message.
                spin = false;
                // Ack the message.
                shimevent_sendEventToShadow(ipc_blk.p, &res);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                // Use provided result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                shimlogger_set_simulation_nanos(
                    res.event_data.syscall_complete.simulation_nanos);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL_DO_NATIVE: {
                // Make the original syscall ourselves and use the result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                const SysCallReg* regs =
                    syscall_event->event_data.syscall.syscall_args.args;
                rv.as_i64 = _real_syscall(
                    syscall_event->event_data.syscall.syscall_args.number,
                    regs[0].as_u64, regs[1].as_u64, regs[2].as_u64,
                    regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // Make the requested syscall ourselves and return the result
                // to Shadow.
                const SysCallReg* regs =
                    res.event_data.syscall.syscall_args.args;
                long syscall_rv = _real_syscall(
                    res.event_data.syscall.syscall_args.number, regs[0].as_u64,
                    regs[1].as_u64, regs[2].as_u64, regs[3].as_u64,
                    regs[4].as_u64, regs[5].as_u64);
                // Recover the true syscall return value from errno in the case
                // of an error.
                if (syscall_rv == -1) {
                    syscall_rv = -errno;
                }
                ShimEvent syscall_complete_event = {
                    .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                    .event_data.syscall_complete.retval.as_i64 = syscall_rv,
                };
                shimevent_sendEventToShadow(ipc_blk.p, &syscall_complete_event);
                break;
            }
            case SHD_SHIM_EVENT_CLONE_REQ:
                shim_shmemHandleClone(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_CLONE_STRING_REQ:
                shim_shmemHandleCloneString(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_WRITE_REQ:
                shim_shmemHandleWrite(&res);
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            case SHD_SHIM_EVENT_SHMEM_COMPLETE:
                shim_shmemNotifyComplete(ipc_blk.p);
                break;
            default: {
                error("Got unexpected event %d", res.event_id);
                abort();
            }
        }
    }
}

static long _vshadow_syscall(long n, va_list args) {
    shim_disableInterposition();
    ShimEvent e = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args.number = n,
    };
    SysCallReg* regs = e.event_data.syscall.syscall_args.args;
    for (int i = 0; i < 6; ++i) {
        regs[i].as_u64 = va_arg(args, uint64_t);
    }
    long rv = shadow_retval_to_errno(_shadow_syscall_event(&e).as_i64);
    shim_enableInterposition();
    return rv;
}

long syscall(long n, ...) {
    shim_ensure_init();
    // Ensure that subsequent stack frames are on a different page than any
    // local variables passed through to the syscall. This ensures that even
    // if any of the syscall arguments are pointers, and those pointers cause
    // shadow to remap the pages containing those pointers, the shim-side stack
    // frames doing that work won't get their memory remapped out from under
    // them.
    void *padding = alloca(sysconf(_SC_PAGE_SIZE));

    // Ensure that the compiler doesn't optimize away `padding`.
    __asm__ __volatile__("" :: "m" (padding));

    va_list(args);
    va_start(args, n);
    long rv;
    if (shim_interpositionEnabled()) {
        debug("Making interposed syscall %ld", n);
        rv = _vshadow_syscall(n, args);
    } else {
        debug("Making real syscall %ld", n);
        rv = _vreal_syscall(n, args);
    }
    va_end(args);
    return rv;
}

// General-case macro for defining a thin wrapper function `fnname` that invokes
// the syscall `sysname`.
#define REMAP(type, fnname, sysname, params, ...)                                                  \
    type fnname params { return (type)syscall(SYS_##sysname, __VA_ARGS__); }

// Same as `REMAP`, but the wrapper function is named after the syscall name.
#define NOREMAP(type, sysname, params, ...) REMAP(type, sysname, sysname, params, __VA_ARGS__)

// Sorted by function name (e.g. using `sort -t',' -k2`).
// clang-format off
NOREMAP(int, accept, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
NOREMAP(int, accept4, (int a, struct sockaddr* b, socklen_t* c, int d), a, b, c, d);
NOREMAP(int, bind, (int a, const struct sockaddr* b, socklen_t c), a,b,c);
NOREMAP(int, clock_gettime, (clockid_t a, struct timespec* b), a,b);
NOREMAP(int, close, (int a), a);
NOREMAP(int, connect, (int a, const struct sockaddr* b, socklen_t c), a,b,c);
NOREMAP(int, creat, (const char *a, mode_t b), a,b);
NOREMAP(int, epoll_create, (int a), a);
NOREMAP(int, epoll_create1, (int a), a);
NOREMAP(int, epoll_ctl, (int a, int b, int c, struct epoll_event* d), a,b,c,d);
NOREMAP(int, epoll_wait, (int a, struct epoll_event* b, int c, int d), a,b,c,d);
//NOREMAP(int, epoll_pwait, (int a, struct epoll_event* b, int c, int d, const sigset_t *e), a,b,c,d,e);
NOREMAP(int, faccessat, (int a, const char *b, int c, int d), a, b, c, d);
NOREMAP(int, fadvise64, (int a, off_t b, off_t c, int d), a, b, c, d);
NOREMAP(int, fallocate, (int a, int b, off_t c, off_t d), a, b, c, d);
NOREMAP(int, fchdir, (int a), a);
NOREMAP(int, fchmod, (int a, mode_t b), a, b);
NOREMAP(int, fchown, (int a, uid_t b, gid_t c), a, b, c);
NOREMAP(int, fchmodat, (int a, const char *b, mode_t c, int d), a, b, c, d);
NOREMAP(int, fchownat, (int a, const char *b, uid_t c, gid_t d, int e), a, b, c, d, e);
static REMAP(int, fcntl_explicit, fcntl, (int a, unsigned long b, char* c), a,b,c);
NOREMAP(int, fdatasync, (int a), a);
NOREMAP(ssize_t, fgetxattr, (int a, const char *b, void *c, size_t d), a, b, c, d);
NOREMAP(ssize_t, flistxattr, (int a, char* b, size_t c), a, b, c);
NOREMAP(int, flock, (int a, int b), a, b);
NOREMAP(int, fremovexattr, (int a, const char* b), a, b);
NOREMAP(int, fsetxattr, (int a, const char* b, const void* c, size_t d, int e), a, b, c, d, e);
NOREMAP(int, fstat, (int a, struct stat* b), a,b);
NOREMAP(int, fstatfs, (int a, struct statfs *b), a, b);
NOREMAP(int, fsync, (int a), a);
NOREMAP(int, ftruncate, (int a, off_t b), a, b);
NOREMAP(int, futimesat, (int a, const char* b, const struct timeval c[2]), a, b, c);
NOREMAP(ssize_t, getdents, (int a, void* b, size_t c), a, b, c);
NOREMAP(ssize_t, getdents64, (int a, void* b, size_t c), a, b, c);
NOREMAP(int, getpeername, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
NOREMAP(ssize_t, getrandom, (void* a, size_t b, unsigned int c), a, b, c);
NOREMAP(int, getsockname, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
NOREMAP(int, getsockopt, (int a, int b, int c, void* d, socklen_t* e), a, b, c, d, e);
static REMAP(int, ioctl_explicit, ioctl, (int a, unsigned long b, char* c), a,b,c);
NOREMAP(int, linkat, (int a, const char* b, int c, const char* d, int e), a, b, c, d, e);
NOREMAP(int, listen, (int a, int b), a, b);
NOREMAP(off_t, lseek, (int a, off_t b, int c), a, b, c);
//NOREMAP(int, lstat, (const char* a, struct stat* b), a,b);
NOREMAP(int, mkdirat, (int a, const char* b, mode_t c), a, b, c);
NOREMAP(int, mknodat, (int a, const char* b, mode_t c, dev_t d), a, b, c, d);
NOREMAP(void*, mmap, (void* a, size_t b, int c, int d, int e, off_t f), a, b, c, d, e, f);
#ifdef SYS_mmap2
NOREMAP(void*, mmap2, (void* a, size_t b, int c, int d, int e, off_t f), a, b, c, d, e, f);
#endif
static REMAP(void*, mremap_explicit, mremap, (void* a, size_t b, size_t c, int d, void* e), a, b, c, d, e);
NOREMAP(void*, munmap, (void* a, size_t b), a, b);
NOREMAP(int, nanosleep, (const struct timespec* a, struct timespec* b), a,b);
NOREMAP(int, newfstatat, (int a, const char* b, struct stat* c, int d), a, b, c, d);
static REMAP(int, openat_explicit, openat, (int a, const char* b, int c, mode_t d), a,b,c,d);
static REMAP(int, open_explicit, open, (const char *a, int b, mode_t c), a,b,c);
NOREMAP(int, pipe, (int a[2]), a);
NOREMAP(int, pipe2, (int a[2], int b), a, b);
//NOREMAP(int, poll, (struct pollfd* a, nfds_t b, int c), a,b,c);
//NOREMAP(int, ppoll, (struct pollfd* a, nfds_t b, const struct timespec* c, const sigset_t* d), a,b,c,d);
NOREMAP(ssize_t, pread64, (int a, void* b, size_t c, off_t d), a, b, c, d);
NOREMAP(ssize_t, preadv, (int a, const struct iovec* b, int c, off_t d), a, b, c, d);
#ifdef SYS_preadv2
NOREMAP(ssize_t, preadv2, (int a, const struct iovec* b, int c, off_t d, int e), a, b, c, d, e);
#endif
NOREMAP(ssize_t, pwrite64, (int a, const void* b, size_t c, off_t d), a, b, c, d);
NOREMAP(ssize_t, pwritev, (int a, const struct iovec* b, int c, off_t d), a, b, c, d);
#ifdef SYS_pwritev2
NOREMAP(ssize_t, pwritev2, (int a, const struct iovec* b, int c, off_t d, int e), a, b, c, d, e);
#endif
NOREMAP(ssize_t, read, (int a, void *b, size_t c), a,b,c);
NOREMAP(ssize_t, readahead, (int a, off64_t b, size_t c), a, b, c);
NOREMAP(ssize_t, readlinkat, (int a, const char* b, char* c, size_t d), a, b, c, d);
NOREMAP(ssize_t, readv, (int a, const struct iovec* b, int c), a, b, c);
NOREMAP(ssize_t, recvfrom, (int a, void* b, size_t c, int d, struct sockaddr* e, socklen_t* f), a,b,c,d,e,f);
//NOREMAP(ssize_t, recvmsg, (int a, struct msghdr* b, int c), a,b,c);
REMAP(ssize_t, recv, recvfrom, (int a, void* b, size_t c, int d), a,b,c,d,NULL,NULL);
NOREMAP(int, renameat, (int a, const char* b, int c, const char* d), a, b, c, d);
NOREMAP(int, renameat2, (int a, const char* b, int c, const char* d, unsigned int e), a, b, c, d, e);
//NOREMAP(ssize_t, sendmsg, (int a, const struct msghdr* b, int c), a,b,c);
REMAP(ssize_t, send, sendto, (int a, const void* b, size_t c, int d), a,b,c,d,NULL,0);
NOREMAP(ssize_t, sendto, (int a, const void* b, size_t c, int d, const struct sockaddr* e, socklen_t f), a,b,c,d,e,f);
NOREMAP(int, setsockopt, (int a, int b, int c, const void *d, socklen_t e), a, b, c, d, e);
NOREMAP(int, shutdown, (int a, int b), a,b);
NOREMAP(int, socket, (int a, int b, int c), a,b,c);
#ifdef SYS_statx
NOREMAP(int, statx, (int a, const char* b, int c, unsigned int d, struct statx* e), a, b, c, d, e);
#endif
NOREMAP(int, symlinkat, (const char* a, int b, const char* c), a, b, c);
NOREMAP(int, sync_file_range, (int a, off64_t b, off64_t c, unsigned int d), a, b, c, d);
NOREMAP(int, syncfs, (int a), a);
NOREMAP(int, uname, (struct utsname* a), a);
NOREMAP(int, unlinkat, (int a, const char *b, int c), a, b, c);
NOREMAP(int, utimensat, (int a, const char* b, const struct timespec c[2], int d), a, b, c, d);
NOREMAP(ssize_t, write, (int a, const void *b, size_t c), a,b,c);
NOREMAP(ssize_t, writev, (int a, const struct iovec* b, int c), a, b, c);
// clang-format on

// TODO: The NOREMAP macro doesn't seem to work with no param list
pid_t getpid() {
    if (shim_interpositionEnabled()) {
        debug("Making interposed syscall getpid");
        return (pid_t)syscall(SYS_getpid);
    } else {
        debug("Making real syscall getpid");
        return (pid_t)_real_syscall(SYS_getpid);
    }
}

/*
 * libc uses variadic functions to implement optional parameters. For those
 * cases, internal versions were created above that take all the parameters
 * explicitly. Next are the variadic wrappers.
 */

int open(const char* pathname, int flags, ...) {
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    return open_explicit(pathname, flags, mode);
}

int openat(int dirfd, const char* pathname, int flags, ...) {
    va_list args;
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);
    return openat_explicit(dirfd, pathname, flags, mode);
}

int ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    char* argp = va_arg(args, char*);
    va_end(args);
    return ioctl_explicit(fd, request, argp);
}

int fcntl(int fd, int command, ...) {
    va_list args;
    va_start(args, command);
    char* argp = va_arg(args, char*);
    va_end(args);
    return fcntl_explicit(fd, command, argp);
}

void* mremap(void* old_address, size_t old_size, size_t new_size, int flags, ...) {
    va_list args;
    va_start(args, flags);
    void* new_address = va_arg(args, void*);
    va_end(args);
    return mremap_explicit(old_address, old_size, new_size, flags, new_address);
}
