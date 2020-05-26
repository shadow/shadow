// Defines system call wrappers: functions that are
// documented in man section 2. (See `man man`).

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "shim/shim.h"
#include "shim/shim_event.h"
#include "shim/shim_logger.h"
#include "shim/shim_shmem.h"
#include "support/logger/logger.h"

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
static long (*_real_syscall)(long n, ...);
__attribute__((constructor(SHIM_CONSTRUCTOR_PRIORITY - 1))) static void
_init_real_syscall() {
    _real_syscall = dlsym(RTLD_NEXT, "syscall");
    assert(_real_syscall != NULL);
}

static long shadow_retval_to_errno(long retval) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (retval <= -1 && retval >= -4095) {
        errno = -retval;
        return -1;
    }
    return retval;
}

static SysCallReg _shadow_syscall_event(const ShimEvent* ev) {
    shim_disableInterposition();

    const int fd = shim_thisThreadEventFD();
    debug("sending syscall event %ld on %d\n",
          ev->event_data.syscall.syscall_args.number, fd);
    shimevent_sendEvent(fd, ev);

    shim_shmemLoop(fd);

    debug("waiting for event on %d\n", fd);
    ShimEvent res;
    shimevent_recvEvent(fd, &res);
    debug("got response on %d\n", fd);
    SysCallReg rv;
    if (res.event_id == SHD_SHIM_EVENT_SYSCALL_COMPLETE) {
        rv = res.event_data.syscall_complete.retval;
        shimlogger_set_simulation_nanos(
            res.event_data.syscall_complete.simulation_nanos);
    } else if (res.event_id == SHD_SHIM_EVENT_SYSCALL_DO_NATIVE) {
        const SysCallReg* regs = ev->event_data.syscall.syscall_args.args;
        rv.as_i64 =
            _real_syscall(ev->event_data.syscall.syscall_args.number,
                          regs[0].as_u64, regs[1].as_u64, regs[2].as_u64,
                          regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
    } else {
        error("Got unexpected event %d", res.event_id);
    }
    shim_enableInterposition();
    return rv;
}

static long _shadow_syscall(ShimEvent* event) {
    return shadow_retval_to_errno(_shadow_syscall_event(event).as_i64);
}

long syscall(long n, ...) {
    ShimEvent e;
    e.event_id = SHD_SHIM_EVENT_SYSCALL;
    e.event_data.syscall.syscall_args.number = n;
    va_list(args);
    va_start(args, n);
    SysCallReg* regs = e.event_data.syscall.syscall_args.args;
    for (int i = 0; i < 6; ++i) {
        regs[i].as_u64 = va_arg(args, uint64_t);
    }
    va_end(args);
    if (shim_interpositionEnabled()) {
        return _shadow_syscall(&e);
    } else {
        return _real_syscall(
            n, regs[0], regs[1], regs[2], regs[3], regs[4], regs[5]);
    }
}

// General-case macro for defining a thin wrapper function `fnname` that invokes
// the syscall `sysname`.
#define REMAP(type, fnname, sysname, params, ...)                              \
    type fnname params {                                                       \
        if (shim_interpositionEnabled()) {                                     \
            debug("Making interposed syscall " #sysname);                      \
            return (type)syscall(SYS_##sysname, __VA_ARGS__);                  \
        } else {                                                               \
            debug("Making real syscall " #sysname);                         \
            return (type)_real_syscall(SYS_##sysname, __VA_ARGS__);            \
        }                                                                      \
    }

// Specialization of REMAP for defining a function `fnname` that invokes a
// syscall of the same name.
#define NOREMAP(type, fnname, params, ...)                                     \
    REMAP(type, fnname, fnname, params, __VA_ARGS__)

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
NOREMAP(int, epoll_pwait, (int a, struct epoll_event* b, int c, int d, const sigset_t *e), a,b,c,d,e);
NOREMAP(int, fstat, (int a, struct stat* b), a,b);
NOREMAP(int, getpeername, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
NOREMAP(int, getsockname, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
static REMAP(int, ioctl_explicit, ioctl, (int a, unsigned long b, char* c), a,b,c);
NOREMAP(int, listen, (int a, int b), a, b);
NOREMAP(int, lstat, (const char* a, struct stat* b), a,b);
NOREMAP(int, nanosleep, (const struct timespec* a, struct timespec* b), a,b);
static REMAP(int, openat_explicit, openat, (int a, const char* b, int c, mode_t d), a,b,c,d);
static REMAP(int, open_explicit, open, (const char *a, int b, mode_t c), a,b,c);
NOREMAP(int, poll, (struct pollfd* a, nfds_t b, int c), a,b,c);
NOREMAP(int, ppoll, (struct pollfd* a, nfds_t b, const struct timespec* c, const sigset_t* d), a,b,c,d);
NOREMAP(ssize_t, read, (int a, void *b, size_t c), a,b,c);
NOREMAP(ssize_t, recvfrom, (int a, void* b, size_t c, int d, struct sockaddr* e, socklen_t* f), a,b,c,d,e,f);
NOREMAP(ssize_t, recvmsg, (int a, struct msghdr* b, int c), a,b,c);
REMAP(ssize_t, recv, recvfrom, (int a, void* b, size_t c, int d), a,b,c,d,NULL,NULL);
NOREMAP(ssize_t, sendmsg, (int a, const struct msghdr* b, int c), a,b,c);
REMAP(ssize_t, send, sendto, (int a, const void* b, size_t c, int d), a,b,c,d,NULL,0);
NOREMAP(ssize_t, sendto, (int a, const void* b, size_t c, int d, const struct sockaddr* e, socklen_t f), a,b,c,d,e,f);
NOREMAP(int, shutdown, (int a, int b), a,b);
NOREMAP(int, socket, (int a, int b, int c), a,b,c);
NOREMAP(int, stat, (const char* a, struct stat* b), a,b);
NOREMAP(int, uname, (struct utsname* a), a);
NOREMAP(ssize_t, write, (int a, const void *b, size_t c), a,b,c);
// clang-format on

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
