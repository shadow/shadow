/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

/// This file contains functions from libc that we want to interpose but that
/// require a bit more code than the minimal syscall and libcall wrappers.
/// Because we do not include interpose.h here, we are free to include other
/// header files as needed. However, we should limit the implementations here as
/// much as possible to very basic logic such as function redirections. Any
/// substantial implementations should be provided in the shim instead, and made
/// available through shim_api.h.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "lib/shim/shim_api.h"

// man 2 syscall
// This function drives all of our wrappers over to the shim.
long syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_api_syscallv(n, args);
    va_end(args);
    return rv;
}

// man 3 localtime
struct tm* localtime(const time_t* timep) {
    // Return time relative to UTC rather than the locale where shadow is being run.
    return gmtime(timep);
}

// man 3 localtime_r
struct tm* localtime_r(const time_t* timep, struct tm* result) {
    // Return time relative to UTC rather than the locale where shadow is being run.
    return gmtime_r(timep, result);
}

// man send
ssize_t send(int sockfd, const void* buf, size_t len, int flags) {
    // An equivalent syscall is available
    return sendto(sockfd, buf, len, flags, NULL, 0);
}

// man recv
ssize_t recv(int sockfd, void* buf, size_t len, int flags) {
    // An equivalent syscall is available
    return recvfrom(sockfd, buf, len, flags, NULL, NULL);
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
    struct timespec rem = {0};

    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }

    return rem.tv_sec;
}

// man gethostname
int gethostname(char* name, size_t len) {
    struct utsname utsname;
    if (uname(&utsname) < 0) {
        return -1;
    }
    strncpy(name, utsname.nodename, len);
    if (len == 0 || name[len - 1] != '\0') {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

// man 3 getaddrinfo
int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints,
                struct addrinfo** res) {
    return shim_api_getaddrinfo(node, service, hints, res);
}

// man 3 freeaddrinfo
void freeaddrinfo(struct addrinfo* res) { return shim_api_freeaddrinfo(res); }

// man 3 getifaddrs
int getifaddrs(struct ifaddrs** ifap) { return shim_api_getifaddrs(ifap); }

// man 3 freeifaddrs
void freeifaddrs(struct ifaddrs* ifa) { return shim_api_freeifaddrs(ifa); }

static void _convert_stat_to_stat64(struct stat* s, struct stat64* s64) {
    memset(s64, 0, sizeof(*s64));

#define COPY_X(x) s64->x = s->x
    COPY_X(st_dev);
    COPY_X(st_ino);
    COPY_X(st_nlink);
    COPY_X(st_mode);
    COPY_X(st_uid);
    COPY_X(st_gid);
    COPY_X(st_rdev);
    COPY_X(st_size);
    COPY_X(st_blksize);
    COPY_X(st_blocks);
    COPY_X(st_atim);
    COPY_X(st_mtim);
    COPY_X(st_ctim);
#undef COPY_X
}

static void _convert_statfs_to_statfs64(struct statfs* s, struct statfs64* s64) {
    memset(s64, 0, sizeof(*s64));

#define COPY_X(x) s64->x = s->x
    COPY_X(f_type);
    COPY_X(f_bsize);
    COPY_X(f_blocks);
    COPY_X(f_bfree);
    COPY_X(f_bavail);
    COPY_X(f_files);
    COPY_X(f_ffree);
    COPY_X(f_fsid);
    COPY_X(f_namelen);
    COPY_X(f_frsize);
    COPY_X(f_flags);
#undef COPY_X
}

// Some platforms define fstat and fstatfs as macros. We should call 'syscall()' directly since
// calling for example 'fstat()' will not necessarily call shadow's 'fstat()' wrapper defined in
// 'preload_syscalls.c'.

int fstat64(int a, struct stat64* b) {
    struct stat s;
    int rv = syscall(SYS_fstat, a, &s);
    _convert_stat_to_stat64(&s, b);
    return rv;
}

int fstatfs64(int a, struct statfs64* b) {
    struct statfs s;
    int rv = syscall(SYS_fstatfs, a, &s);
    _convert_statfs_to_statfs64(&s, b);
    return rv;
}

int __fxstat(int ver, int a, struct stat* b) {
    // on x86_64 with a modern kernel, glibc should use the same stat struct as the kernel, so check
    // that this function was indeed called with the expected stat struct
    if (ver != 1 /* _STAT_VER_KERNEL for x86_64 */) {
        assert("__fxstat called with unexpected ver" && ver && 0);
        errno = EINVAL;
        return -1;
    }

    return syscall(SYS_fstat, a, b);
}

int __fxstat64(int ver, int a, struct stat64* b) {
    // on x86_64 with a modern kernel, glibc should use the same stat struct as the kernel, so check
    // that this function was indeed called with the expected stat struct
    if (ver != 1 /* _STAT_VER_KERNEL for x86_64 */) {
        assert("__fxstat64 called with unexpected ver" && ver && 0);
        errno = EINVAL;
        return -1;
    }

    struct stat s;
    int rv = syscall(SYS_fstat, a, &s);
    _convert_stat_to_stat64(&s, b);
    return rv;
}

int open(const char* pathname, int flags, ...) {
    va_list(args);
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);

    // "If neither O_CREAT nor O_TMPFILE is specified in flags, then mode is
    // ignored"
    //
    // We explicitly set to 0 here so that strace logging doesn't log an
    // arbitrary value for `mode` when it wasn't explicitly provided by the
    // caller.
    if (!(flags & (O_CREAT | O_TMPFILE))) {
        mode = 0;
    }

    return (int)syscall(SYS_open, pathname, flags, mode);
}

int openat(int dirfd, const char* pathname, int flags, ...) {
    va_list(args);
    va_start(args, flags);
    mode_t mode = va_arg(args, mode_t);
    va_end(args);

    // "If neither O_CREAT nor O_TMPFILE is specified in flags, then mode is
    // ignored"
    //
    // We explicitly set to 0 here so that strace logging doesn't log an
    // arbitrary value for `mode` when it wasn't explicitly provided by the
    // caller.
    if (!(flags & (O_CREAT | O_TMPFILE))) {
        mode = 0;
    }

    return (int)syscall(SYS_openat, dirfd, pathname, flags, mode);
}