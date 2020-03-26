#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "shim.h"
#include "shim-event.h"
#include "system-libc.h"

static long shadow_retval_to_errno(long retval) {
    if (retval >= 0) {
        return retval;
    } else {
        errno = -retval;
        return -1;
    }
}

static SysCallReg shadow_syscall(const ShimEvent* ev) {
    const int fd = shim_thisThreadEventFD();
    SHD_SHIM_LOG("sending event on %d\n", fd);
    shimevent_sendEvent(fd, ev);

    SHD_SHIM_LOG("waiting for event on %d\n", fd);
    ShimEvent res;
    shimevent_recvEvent(fd, &res);
    SHD_SHIM_LOG("got response on %d\n", fd);
    assert(res.event_id == SHD_SHIM_EVENT_SYSCALL_COMPLETE);

    return res.event_data.syscall_complete.retval;
}

static long shadow_syscall_nanosleep(va_list args) {
    const struct timespec* req = va_arg(args, const struct timespec*);
    struct timespec* res = va_arg(args, struct timespec*);

    // FIXME: the real ABI uses pointers to timespecs.
    // Switch to that when we have shared memory implemented.
    // In particular we don't write remaining time to `rem` yet.
    ShimEvent event = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args =
            (SysCallArgs){.number = SYS_nanosleep,
                          .args = {req->tv_sec, req->tv_nsec}}
    };
    return shadow_retval_to_errno(shadow_syscall(&event).as_i64);
}

static long shadow_syscall_clock_gettime(va_list args) {
    clockid_t clk_id = va_arg(args, clockid_t);
    struct timespec* res = va_arg(args, struct timespec*);

    ShimEvent event = {.event_id = SHD_SHIM_EVENT_SYSCALL,
                       .event_data.syscall.syscall_args = (SysCallArgs){
                           .number = SYS_clock_gettime,
                           .args = {clk_id, {.as_u64 = (uint64_t)res}}}};
    int64_t rv = shadow_syscall(&event).as_i64;

    // FIXME: the real ABI uses pointers to timespecs.
    // Switch to that when we have shared memory implemented.
    // In the meantime, shadow passes the result as literal nanos.
    const int64_t nano = 1000000000LL;
    res->tv_sec = rv / nano; 
    res->tv_nsec = rv % nano;
    return 0;
}

static long _vreal_syscall(long n, va_list args) {
    long arg1 = va_arg(args, long);
    long arg2 = va_arg(args, long);
    long arg3 = va_arg(args, long);
    long arg4 = va_arg(args, long);
    long arg5 = va_arg(args, long);
    long arg6 = va_arg(args, long);
    return system_libc_syscall(n, arg1, arg2, arg3, arg4, arg5, arg6);
}

// man 2 syscall
long syscall(long n, ...) {
    va_list args;
    va_start(args, n);
    if (!shim_usingInterposePreload()) {
        return _vreal_syscall(n, args);
    }
    long rv = -1;
    switch (n) {
        case SYS_nanosleep:
            rv = shadow_syscall_nanosleep(args);
            break;
        case SYS_clock_gettime:
            rv = shadow_syscall_clock_gettime(args);
            break;
        default:
            SHD_SHIM_LOG("unhandled syscall %ld\n", n);
    }
    va_end(args);
    return rv;
}

// man 2 nanosleep
int nanosleep(const struct timespec* req, struct timespec* rem) {
    return syscall(SYS_nanosleep, req, rem);
}

// man 3 usleep
int usleep(useconds_t usec) {
    struct timespec req, rem;
    req.tv_sec = usec / 1000000;
    const long remaining_usec = usec - req.tv_sec * 1000000;
    req.tv_nsec = remaining_usec * 1000;

    return nanosleep(&req, &rem);
}

// man 3 sleep
unsigned int sleep(unsigned int seconds) {
    struct timespec req = {.tv_sec = seconds};
    struct timespec rem = { 0 };

    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }

    return rem.tv_sec;
}

// man 2 
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    return syscall(SYS_clock_gettime, clk_id, tp);
}

