/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <fcntl.h>

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <poll.h>
#include <malloc.h>
#include <ifaddrs.h>
#include <sys/epoll.h>
//#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <linux/sockios.h>
#include <features.h>

#include <malloc.h>
#include <pthread.h>

#include "shadow.h"

#define SETSYM(funcptr, funcstr) {funcptr = dlsym(RTLD_NEXT, funcstr);}

#define SETSYM_OR_FAIL(funcptr, funcstr) { \
    dlerror(); \
    SETSYM(funcptr, funcstr);\
    char* errorMessage = dlerror(); \
    if(errorMessage != NULL) { \
        fprintf(stderr, "dlsym(%s): dlerror(): %s\n", funcstr, errorMessage); \
        exit(EXIT_FAILURE); \
    } else if(funcptr == NULL) { \
        fprintf(stderr, "dlsym(%s): returned NULL pointer\n", funcstr); \
        exit(EXIT_FAILURE); \
    } \
}

#define ENSURE(func) { \
    if(!director.next.func) { \
        SETSYM_OR_FAIL(director.next.func, #func); \
    } \
}

#define INTERPOSE_HELPER(prototype, rtrnstmt, fnctname, ...) \
prototype { \
    Process* proc = NULL; \
    if((proc = _doEmulate()) != NULL) { \
        rtrnstmt process_emu_##fnctname(proc, ##__VA_ARGS__); \
    } else { \
        ENSURE(fnctname); \
        rtrnstmt director.next.fnctname(__VA_ARGS__); \
    } \
}

#define INTERPOSE(prototype, fnctname, ...) INTERPOSE_HELPER(prototype, return, fnctname, ##__VA_ARGS__);
#define INTERPOSE_NORET(prototype, fnctname, ...) INTERPOSE_HELPER(prototype, , fnctname, ##__VA_ARGS__);

#if !defined __USE_LARGEFILE64
typedef off_t off64_t;
#endif

/* memory allocation family */

typedef void* (*malloc_func)(size_t);
typedef void* (*calloc_func)(size_t, size_t);
typedef void* (*realloc_func)(void*, size_t);
typedef int (*posix_memalign_func)(void**, size_t, size_t);
typedef void* (*memalign_func)(size_t, size_t);
typedef void* (*aligned_alloc_func)(size_t, size_t);
typedef void* (*valloc_func)(size_t);
typedef void* (*pvalloc_func)(size_t);
typedef void (*free_func)(void*);
typedef void* (*mmap_func)(void *, size_t, int, int, int, off_t);

/* event family */

typedef int (*epoll_create_func)(int);
typedef int (*epoll_create1_func)(int);
typedef int (*epoll_ctl_func)(int, int, int, struct epoll_event*);
typedef int (*epoll_wait_func)(int, struct epoll_event*, int, int);
typedef int (*epoll_pwait_func)(int, struct epoll_event*, int, int, const sigset_t*);
typedef int (*eventfd_func)(int, int);

/* socket/io family */

typedef int (*socket_func)(int, int, int);
typedef int (*socketpair_func)(int, int, int, int[]);
typedef int (*bind_func)(int, __CONST_SOCKADDR_ARG, socklen_t);
typedef int (*getsockname_func)(int, __SOCKADDR_ARG, socklen_t*);
typedef int (*connect_func)(int, __CONST_SOCKADDR_ARG, socklen_t);
typedef int (*getpeername_func)(int, __SOCKADDR_ARG, socklen_t*);
typedef size_t (*send_func)(int, const void*, size_t, int);
typedef size_t (*sendto_func)(int, const void*, size_t, int, __CONST_SOCKADDR_ARG, socklen_t);
typedef size_t (*sendmsg_func)(int, const struct msghdr*, int);
typedef size_t (*recv_func)(int, void*, size_t, int);
typedef size_t (*recvfrom_func)(int, void*, size_t, int, __SOCKADDR_ARG, socklen_t*);
typedef size_t (*recvmsg_func)(int, struct msghdr*, int);
typedef int (*getsockopt_func)(int, int, int, void*, socklen_t*);
typedef int (*setsockopt_func)(int, int, int, const void*, socklen_t);
typedef int (*listen_func)(int, int);
typedef int (*accept_func)(int, __SOCKADDR_ARG, socklen_t*);
typedef int (*accept4_func)(int, __SOCKADDR_ARG, socklen_t*, int);
typedef int (*shutdown_func)(int, int);
typedef int (*pipe_func)(int [2]);
typedef int (*pipe2_func)(int [2], int);
typedef size_t (*read_func)(int, void*, size_t);
typedef size_t (*write_func)(int, const void*, size_t);
typedef ssize_t (*readv_func)(int, const struct iovec*, int);
typedef ssize_t (*writev_func)(int, const struct iovec*, int);
typedef int (*close_func)(int);
typedef int (*fcntl_func)(int, int, ...);
typedef int (*ioctl_func)(int, int, ...);
typedef int (*getifaddrs_func)(struct ifaddrs**);
typedef void (*freeifaddrs_func)(struct ifaddrs*);

/* polling */

typedef unsigned int (*sleep_func)(unsigned int);
typedef int (*nanosleep_func)(const struct timespec *, struct timespec *);
typedef int (*usleep_func)(unsigned int);
typedef int (*select_func)(int, fd_set *, fd_set *, fd_set *, struct timeval *);
typedef int (*pselect_func)(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);
typedef int (*poll_func)(struct pollfd*, nfds_t, int);
typedef int (*ppoll_func)(struct pollfd*, nfds_t, const struct timespec*, const sigset_t*);
typedef int (*system_func)(const char *);
typedef pid_t (*fork_func)(void);
typedef int (*sigwait_func)(const sigset_t *, int *);
typedef pid_t (*waitpid_func)(pid_t, int *, int);

/* timers */

typedef int (*timerfd_create_func)(int, int);
typedef int (*timerfd_settime_func)(int, int, const struct itimerspec*, struct itimerspec*);
typedef int (*timerfd_gettime_func)(int, struct itimerspec*);

/* file specific */

typedef int (*fileno_func)(FILE *);
typedef int (*open_func)(const char*, int, mode_t);
typedef int (*open64_func)(const char*, int, mode_t);
typedef int (*creat_func)(const char*, mode_t);
typedef FILE* (*fopen_func)(const char *, const char *);
typedef FILE* (*fopen64_func)(const char *, const char *);
typedef FILE* (*fdopen_func)(int, const char*);
typedef int (*fclose_func)(FILE *);
typedef int (*dup_func)(int);
typedef int (*dup2_func)(int, int);
typedef int (*dup3_func)(int, int, int);
typedef int (*__fxstat_func)(int, int, struct stat*);
typedef int (*__fxstat64_func)(int, int, struct stat64*);
typedef int (*fstatfs_func)(int, struct statfs*);
typedef int (*fstatfs64_func)(int, struct statfs64*);
typedef off_t (*lseek_func)(int, off_t, int);
typedef off64_t (*lseek64_func)(int, off64_t, int);
typedef size_t (*pread_func)(int, void*, size_t, off_t);
typedef ssize_t (*pwrite_func)(int, const void *, size_t, off_t);
typedef int (*flock_func)(int, int);
typedef int (*fsync_func)(int);
typedef int (*ftruncate_func)(int, int);
typedef int (*ftruncate64_func)(int, int);
typedef int (*posix_fallocate_func)(int, int, int);

typedef int (*fstatvfs_func)(int, struct statvfs*);
typedef int (*fdatasync_func)(int);
typedef int (*syncfs_func)(int);
typedef int (*fallocate_func)(int, int, off_t, off_t);
typedef int (*fexecve_func)(int, char *const argv[], char *const envp[]);
typedef long (*fpathconf_func)(int, int);
typedef int (*fchdir_func)(int);
typedef int (*fchown_func)(int, uid_t, gid_t);
typedef int (*fchmod_func)(int, mode_t);
typedef int (*posix_fadvise_func)(int, off_t, off_t, int);
typedef int (*lockf_func)(int, int, off_t);
typedef int (*openat_func)(int, const char*, int, mode_t);
typedef int (*faccessat_func)(int, const char*, int, int);
typedef int (*unlinkat_func)(int, const char*, int);
typedef int (*fchmodat_func)(int, const char*, mode_t, int);
typedef int (*fchownat_func)(int, const char*, uid_t, gid_t, int);

typedef size_t (*fread_func)(void *, size_t, size_t, FILE *);
typedef size_t (*fwrite_func)(const void *, size_t, size_t, FILE *);
typedef int (*fputc_func)(int, FILE *);
typedef int (*fputs_func)(const char *, FILE *);
typedef int (*putchar_func)(int);
typedef int (*puts_func)(const char *);
typedef int (*printf_func)(const char *, ...);
typedef int (*vprintf_func)(const char *, va_list);
typedef int (*fprintf_func)(FILE *, const char *, ...);
typedef int (*vfprintf_func)(FILE *, const char *, va_list);
typedef int (*fflush_func)(FILE *);

/* time family */

typedef time_t (*time_func)(time_t*);
typedef int (*clock_gettime_func)(clockid_t, struct timespec *);
typedef int (*gettimeofday_func)(struct timeval*, struct timezone*);
typedef struct tm *(*localtime_func)(const time_t *timep);
typedef struct tm *(*localtime_r_func)(const time_t *timep, struct tm *result);

/* name/address family */

typedef int (*gethostname_func)(char*, size_t);
typedef int (*getaddrinfo_func)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
typedef int (*freeaddrinfo_func)(struct addrinfo*);
typedef int (*getnameinfo_func)(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int);
typedef struct hostent* (*gethostbyname_func)(const char*);
typedef int (*gethostbyname_r_func)(const char*, struct hostent*, char*, size_t, struct hostent**, int*);
typedef struct hostent* (*gethostbyname2_func)(const char*, int);
typedef int (*gethostbyname2_r_func)(const char*, int, struct hostent *, char *, size_t, struct hostent**, int*);
typedef struct hostent* (*gethostbyaddr_func)(const void*, socklen_t, int);
typedef int (*gethostbyaddr_r_func)(const void*, socklen_t, int, struct hostent*, char*, size_t, struct hostent **, int*);

/* random family */

typedef int (*rand_func)();
typedef int (*rand_r_func)(unsigned int*);
typedef void (*srand_func)(unsigned int);
typedef long int (*random_func)(void);
typedef int (*random_r_func)(struct random_data*, int32_t*);
typedef void (*srandom_func)(unsigned int);
typedef int (*srandom_r_func)(unsigned int, struct random_data*);

/* exit family */

typedef void (*exit_func)(int status);
typedef int (*on_exit_func)(void (*function)(int , void *), void *arg);
typedef int (*atexit_func)(void (*func)(void));
typedef int (*__cxa_atexit_func)(void (*func) (void *), void * arg, void * dso_handle);

/* pthread thread attribute */

typedef int (*pthread_attr_init_func)(pthread_attr_t *);
typedef int (*pthread_attr_destroy_func)(pthread_attr_t *);
typedef int (*pthread_attr_setinheritsched_func)(pthread_attr_t *, int);
typedef int (*pthread_attr_getinheritsched_func)(const pthread_attr_t *, int *);
typedef int (*pthread_attr_setschedparam_func)(pthread_attr_t *, const struct sched_param *);
typedef int (*pthread_attr_getschedparam_func)(const pthread_attr_t *, struct sched_param *);
typedef int (*pthread_attr_setschedpolicy_func)(pthread_attr_t *, int);
typedef int (*pthread_attr_getschedpolicy_func)(const pthread_attr_t *, int *);
typedef int (*pthread_attr_setscope_func)(pthread_attr_t *, int);
typedef int (*pthread_attr_getscope_func)(const pthread_attr_t *, int *);
typedef int (*pthread_attr_setstacksize_func)(pthread_attr_t *, size_t);
typedef int (*pthread_attr_getstacksize_func)(const pthread_attr_t *, size_t *);
typedef int (*pthread_attr_setstackaddr_func)(pthread_attr_t *, void *);
typedef int (*pthread_attr_getstackaddr_func)(const pthread_attr_t *, void **);
typedef int (*pthread_attr_setdetachstate_func)(pthread_attr_t *, int);
typedef int (*pthread_attr_getdetachstate_func)(const pthread_attr_t *, int *);
typedef int (*pthread_attr_setguardsize_func)(pthread_attr_t *, size_t);
typedef int (*pthread_attr_getguardsize_func)(const pthread_attr_t *, size_t *);
typedef int (*pthread_attr_setname_np_func)(pthread_attr_t *, char *);
typedef int (*pthread_attr_getname_np_func)(const pthread_attr_t *, char **);
typedef int (*pthread_attr_setprio_np_func)(pthread_attr_t *, int);
typedef int (*pthread_attr_getprio_np_func)(const pthread_attr_t *, int *);

/* pthread thread */

typedef int (*pthread_create_func)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
typedef int (*pthread_detach_func)(pthread_t);
typedef pthread_t (*pthread_self_func)(void);
typedef int (*pthread_equal_func)(pthread_t, pthread_t);
typedef int (*pthread_yield_func)(void);
typedef void (*pthread_exit_func)(void *);
typedef int (*pthread_join_func)(pthread_t, void **);
typedef int (*pthread_once_func)(pthread_once_t *, void (*)(void));
typedef int (*pthread_sigmask_func)(int, const sigset_t *, sigset_t *);
typedef int (*pthread_kill_func)(pthread_t, int);
typedef int (*pthread_abort_func)(pthread_t);

/* pthread concurrency */

typedef int (*pthread_getconcurrency_func)(void);
typedef int (*pthread_setconcurrency_func)(int);

/* pthread context */

typedef int (*pthread_key_create_func)(pthread_key_t *, void (*)(void *));
typedef int (*pthread_key_delete_func)(pthread_key_t);
typedef int (*pthread_setspecific_func)(pthread_key_t, const void *);
typedef void * (*pthread_getspecific_func)(pthread_key_t);

/* pthread cancel */

typedef int (*pthread_cancel_func)(pthread_t);
typedef void (*pthread_testcancel_func)(void);
typedef int (*pthread_setcancelstate_func)(int, int *);
typedef int (*pthread_setcanceltype_func)(int, int *);

/* pthread scheduler */

typedef int (*pthread_setschedparam_func)(pthread_t, int, const struct sched_param *);
typedef int (*pthread_getschedparam_func)(pthread_t, int *, struct sched_param *);

/* pthread cleanup */

typedef void (*pthread_cleanup_push_func)(void (*)(void *), void *);
typedef void (*pthread_cleanup_pop_func)(int);
typedef int (*pthread_atfork_func)(void (*)(void), void (*)(void), void (*)(void));

/* pthread mutex attribute */

typedef int (*pthread_mutexattr_init_func)(pthread_mutexattr_t *);
typedef int (*pthread_mutexattr_destroy_func)(pthread_mutexattr_t *);
typedef int (*pthread_mutexattr_setprioceiling_func)(pthread_mutexattr_t *, int);
typedef int (*pthread_mutexattr_getprioceiling_func)(const pthread_mutexattr_t *, int *);
typedef int (*pthread_mutexattr_setprotocol_func)(pthread_mutexattr_t *, int);
typedef int (*pthread_mutexattr_getprotocol_func)(const pthread_mutexattr_t *, int *);
typedef int (*pthread_mutexattr_setpshared_func)(pthread_mutexattr_t *, int);
typedef int (*pthread_mutexattr_getpshared_func)(const pthread_mutexattr_t *, int *);
typedef int (*pthread_mutexattr_settype_func)(pthread_mutexattr_t *, int);
typedef int (*pthread_mutexattr_gettype_func)(const pthread_mutexattr_t *, int *);

/* pthread mutex */

typedef int (*pthread_mutex_init_func)(pthread_mutex_t *, const pthread_mutexattr_t *);
typedef int (*pthread_mutex_destroy_func)(pthread_mutex_t *);
typedef int (*pthread_mutex_setprioceiling_func)(pthread_mutex_t *, int, int *);
typedef int (*pthread_mutex_getprioceiling_func)(const pthread_mutex_t *, int *);
typedef int (*pthread_mutex_lock_func)(pthread_mutex_t *);
typedef int (*pthread_mutex_trylock_func)(pthread_mutex_t *);
typedef int (*pthread_mutex_unlock_func)(pthread_mutex_t *);

/* pthread rwlock attribute */

typedef int (*pthread_rwlockattr_init_func)(pthread_rwlockattr_t *);
typedef int (*pthread_rwlockattr_destroy_func)(pthread_rwlockattr_t *);
typedef int (*pthread_rwlockattr_setpshared_func)(pthread_rwlockattr_t *, int);
typedef int (*pthread_rwlockattr_getpshared_func)(const pthread_rwlockattr_t *, int *);

/* pthread rwlock */

typedef int (*pthread_rwlock_init_func)(pthread_rwlock_t *, const pthread_rwlockattr_t *);
typedef int (*pthread_rwlock_destroy_func)(pthread_rwlock_t *);
typedef int (*pthread_rwlock_rdlock_func)(pthread_rwlock_t *);
typedef int (*pthread_rwlock_tryrdlock_func)(pthread_rwlock_t *);
typedef int (*pthread_rwlock_wrlock_func)(pthread_rwlock_t *);
typedef int (*pthread_rwlock_trywrlock_func)(pthread_rwlock_t *);
typedef int (*pthread_rwlock_unlock_func)(pthread_rwlock_t *);

/* pthread condition attribute */

typedef int (*pthread_condattr_init_func)(pthread_condattr_t *);
typedef int (*pthread_condattr_destroy_func)(pthread_condattr_t *);
typedef int (*pthread_condattr_setpshared_func)(pthread_condattr_t *, int);
typedef int (*pthread_condattr_getpshared_func)(const pthread_condattr_t *, int *);
typedef int (*pthread_condattr_setclock_func)(pthread_condattr_t *, clockid_t);
typedef int (*pthread_condattr_getclock_func)(const pthread_condattr_t *, clockid_t*);

/* pthread condition */

typedef int (*pthread_cond_init_func)(pthread_cond_t *, const pthread_condattr_t *);
typedef int (*pthread_cond_destroy_func)(pthread_cond_t *);
typedef int (*pthread_cond_broadcast_func)(pthread_cond_t *);
typedef int (*pthread_cond_signal_func)(pthread_cond_t *);
typedef int (*pthread_cond_wait_func)(pthread_cond_t *, pthread_mutex_t *);
typedef int (*pthread_cond_timedwait_func)(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);


typedef struct {
    malloc_func malloc;
    calloc_func calloc;
    realloc_func realloc;
    posix_memalign_func posix_memalign;
    memalign_func memalign;
    aligned_alloc_func aligned_alloc;
    valloc_func valloc;
    pvalloc_func pvalloc;
    free_func free;
    mmap_func mmap;

    epoll_create_func epoll_create;
    epoll_create1_func epoll_create1;
    epoll_ctl_func epoll_ctl;
    epoll_wait_func epoll_wait;
    epoll_pwait_func epoll_pwait;
    eventfd_func eventfd;

    timerfd_create_func timerfd_create;
    timerfd_settime_func timerfd_settime;
    timerfd_gettime_func timerfd_gettime;

    socket_func socket;
    socketpair_func socketpair;
    bind_func bind;
    getsockname_func getsockname;
    connect_func connect;
    getpeername_func getpeername;
    send_func send;
    sendto_func sendto;
    sendmsg_func sendmsg;
    recv_func recv;
    recvfrom_func recvfrom;
    recvmsg_func recvmsg;
    getsockopt_func getsockopt;
    setsockopt_func setsockopt;
    listen_func listen;
    accept_func accept;
    accept4_func accept4;
    shutdown_func shutdown;
    pipe_func pipe;
    pipe2_func pipe2;
    read_func read;
    write_func write;
    readv_func readv;
    writev_func writev;
    close_func close;
    fcntl_func fcntl;
    ioctl_func ioctl;
    getifaddrs_func getifaddrs;
    freeifaddrs_func freeifaddrs;

    sleep_func sleep;
    nanosleep_func nanosleep;
    usleep_func usleep;
    select_func select;
    pselect_func pselect;
    poll_func poll;
    ppoll_func ppoll;
    system_func system;
    fork_func fork;
    sigwait_func sigwait;
    waitpid_func waitpid;

    fileno_func fileno;
    open_func open;
    open64_func open64;
    creat_func creat;
    fopen_func fopen;
    fopen64_func fopen64;
    fdopen_func fdopen;
    dup_func dup;
    dup2_func dup2;
    dup3_func dup3;
    fclose_func fclose;
    __fxstat_func __fxstat;
    __fxstat64_func __fxstat64;
    fstatfs_func fstatfs;
    fstatfs64_func fstatfs64;
    lseek_func lseek;
    lseek64_func lseek64;
    pread_func pread;
    pwrite_func pwrite;
    flock_func flock;
    fsync_func fsync;
    ftruncate_func ftruncate;
    ftruncate64_func ftruncate64;
    posix_fallocate_func posix_fallocate;
    fstatvfs_func fstatvfs;
    fdatasync_func fdatasync;
    syncfs_func syncfs;
    fallocate_func fallocate;
    fexecve_func fexecve;
    fpathconf_func fpathconf;
    fchdir_func fchdir;
    fchown_func fchown;
    fchmod_func fchmod;
    posix_fadvise_func posix_fadvise;
    lockf_func lockf;
    openat_func openat;
    faccessat_func faccessat;
    unlinkat_func unlinkat;
    fchmodat_func fchmodat;
    fchownat_func fchownat;

    fread_func fread;
    fwrite_func fwrite;
    fputc_func fputc;
    fputs_func fputs;
    putchar_func putchar;
    puts_func puts;
    printf_func printf;
    vprintf_func vprintf;
    fprintf_func fprintf;
    vfprintf_func vfprintf;
    fflush_func fflush;

    time_func time;
    clock_gettime_func clock_gettime;
    gettimeofday_func gettimeofday;
    localtime_func localtime;
    localtime_r_func localtime_r;

    gethostname_func gethostname;
    getaddrinfo_func getaddrinfo;
    freeaddrinfo_func freeaddrinfo;
    getnameinfo_func getnameinfo;
    gethostbyname_func gethostbyname;
    gethostbyname_r_func gethostbyname_r;
    gethostbyname2_func gethostbyname2;
    gethostbyname2_r_func gethostbyname2_r;
    gethostbyaddr_func gethostbyaddr;
    gethostbyaddr_r_func gethostbyaddr_r;

    rand_func rand;
    rand_r_func rand_r;
    srand_func srand;
    random_func random;
    random_r_func random_r;
    srandom_func srandom;
    srandom_r_func srandom_r;

    exit_func exit;
    on_exit_func on_exit;
    atexit_func atexit;
    __cxa_atexit_func __cxa_atexit;

    pthread_attr_init_func pthread_attr_init;
    pthread_attr_destroy_func pthread_attr_destroy;
    pthread_attr_setinheritsched_func pthread_attr_setinheritsched;
    pthread_attr_getinheritsched_func pthread_attr_getinheritsched;
    pthread_attr_setschedparam_func pthread_attr_setschedparam;
    pthread_attr_getschedparam_func pthread_attr_getschedparam;
    pthread_attr_setschedpolicy_func pthread_attr_setschedpolicy;
    pthread_attr_getschedpolicy_func pthread_attr_getschedpolicy;
    pthread_attr_setscope_func pthread_attr_setscope;
    pthread_attr_getscope_func pthread_attr_getscope;
    pthread_attr_setstacksize_func pthread_attr_setstacksize;
    pthread_attr_getstacksize_func pthread_attr_getstacksize;
    pthread_attr_setstackaddr_func pthread_attr_setstackaddr;
    pthread_attr_getstackaddr_func pthread_attr_getstackaddr;
    pthread_attr_setdetachstate_func pthread_attr_setdetachstate;
    pthread_attr_getdetachstate_func pthread_attr_getdetachstate;
    pthread_attr_setguardsize_func pthread_attr_setguardsize;
    pthread_attr_getguardsize_func pthread_attr_getguardsize;
    pthread_attr_setname_np_func pthread_attr_setname_np;
    pthread_attr_getname_np_func pthread_attr_getname_np;
    pthread_attr_setprio_np_func pthread_attr_setprio_np;
    pthread_attr_getprio_np_func pthread_attr_getprio_np;

    pthread_create_func pthread_create;
    pthread_detach_func pthread_detach;
    pthread_detach_func __pthread_detach;
    pthread_self_func pthread_self;
    pthread_equal_func pthread_equal;
    pthread_yield_func pthread_yield;
    pthread_yield_func pthread_yield_np;
    pthread_exit_func pthread_exit;
    pthread_join_func pthread_join;
    pthread_once_func pthread_once;
    pthread_sigmask_func pthread_sigmask;
    pthread_kill_func pthread_kill;
    pthread_abort_func pthread_abort;

    pthread_getconcurrency_func pthread_getconcurrency;
    pthread_setconcurrency_func pthread_setconcurrency;

    pthread_key_create_func pthread_key_create;
    pthread_key_delete_func pthread_key_delete;
    pthread_setspecific_func pthread_setspecific;
    pthread_getspecific_func pthread_getspecific;

    pthread_cancel_func pthread_cancel;
    pthread_testcancel_func pthread_testcancel;
    pthread_setcancelstate_func pthread_setcancelstate;
    pthread_setcanceltype_func pthread_setcanceltype;

    pthread_setschedparam_func pthread_setschedparam;
    pthread_getschedparam_func pthread_getschedparam;

    pthread_cleanup_push_func pthread_cleanup_push;
    pthread_cleanup_pop_func pthread_cleanup_pop;
    pthread_atfork_func pthread_atfork;

    pthread_mutexattr_init_func pthread_mutexattr_init;
    pthread_mutexattr_destroy_func pthread_mutexattr_destroy;
    pthread_mutexattr_setprioceiling_func pthread_mutexattr_setprioceiling;
    pthread_mutexattr_getprioceiling_func pthread_mutexattr_getprioceiling;
    pthread_mutexattr_setprotocol_func pthread_mutexattr_setprotocol;
    pthread_mutexattr_getprotocol_func pthread_mutexattr_getprotocol;
    pthread_mutexattr_setpshared_func pthread_mutexattr_setpshared;
    pthread_mutexattr_getpshared_func pthread_mutexattr_getpshared;
    pthread_mutexattr_settype_func pthread_mutexattr_settype;
    pthread_mutexattr_gettype_func pthread_mutexattr_gettype;

    pthread_mutex_init_func pthread_mutex_init;
    pthread_mutex_destroy_func pthread_mutex_destroy;
    pthread_mutex_setprioceiling_func pthread_mutex_setprioceiling;
    pthread_mutex_getprioceiling_func pthread_mutex_getprioceiling;
    pthread_mutex_lock_func pthread_mutex_lock;
    pthread_mutex_trylock_func pthread_mutex_trylock;
    pthread_mutex_unlock_func pthread_mutex_unlock;

    pthread_rwlockattr_init_func pthread_rwlockattr_init;
    pthread_rwlockattr_destroy_func pthread_rwlockattr_destroy;
    pthread_rwlockattr_setpshared_func pthread_rwlockattr_setpshared;
    pthread_rwlockattr_getpshared_func pthread_rwlockattr_getpshared;

    pthread_rwlock_init_func pthread_rwlock_init;
    pthread_rwlock_destroy_func pthread_rwlock_destroy;
    pthread_rwlock_rdlock_func pthread_rwlock_rdlock;
    pthread_rwlock_tryrdlock_func pthread_rwlock_tryrdlock;
    pthread_rwlock_wrlock_func pthread_rwlock_wrlock;
    pthread_rwlock_trywrlock_func pthread_rwlock_trywrlock;
    pthread_rwlock_unlock_func pthread_rwlock_unlock;

    pthread_condattr_init_func pthread_condattr_init;
    pthread_condattr_destroy_func pthread_condattr_destroy;
    pthread_condattr_setpshared_func pthread_condattr_setpshared;
    pthread_condattr_getpshared_func pthread_condattr_getpshared;
    pthread_condattr_setclock_func pthread_condattr_setclock;
    pthread_condattr_getclock_func pthread_condattr_getclock;

    pthread_cond_init_func pthread_cond_init;
    pthread_cond_destroy_func pthread_cond_destroy;
    pthread_cond_broadcast_func pthread_cond_broadcast;
    pthread_cond_signal_func pthread_cond_signal;
    pthread_cond_wait_func pthread_cond_wait;
    pthread_cond_timedwait_func pthread_cond_timedwait;
} PreloadFuncs;

typedef struct {
    struct {
        char buf[102400];
        size_t pos;
        size_t nallocs;
        size_t ndeallocs;
    } dummy;
    PreloadFuncs next;
    int shadowIsLoaded;
} FuncDirector;

/* global storage for function pointers that we look up lazily */
static FuncDirector director;
static int directorIsInitialized;

/* track if we are in a recursive loop to avoid infinite recursion.
 * threads MUST access this via &isRecursive to ensure each has its own copy
 * http://gcc.gnu.org/onlinedocs/gcc-4.3.6/gcc/Thread_002dLocal.html */
static __thread unsigned long isRecursive = 0;

/* provide a way to disable and enable interposition */
static __thread unsigned long disableCount = 0;

/* we must use the & operator to get the current thread's version */
void interposer_enable() {__sync_fetch_and_sub(&disableCount, 1);}
void interposer_disable() {__sync_fetch_and_add(&disableCount, 1);}

static void* dummy_malloc(size_t size) {
    if (director.dummy.pos + size >= sizeof(director.dummy.buf)) {
        exit(EXIT_FAILURE);
    }
    void* mem = &(director.dummy.buf[director.dummy.pos]);
    director.dummy.pos += size;
    director.dummy.nallocs++;
    return mem;
}

static void* dummy_calloc(size_t nmemb, size_t size) {
    size_t total_bytes = nmemb * size;
    void* mem = dummy_malloc(total_bytes);
    memset(mem, 0, total_bytes);
    return mem;
}

static void dummy_free(void *ptr) {
    director.dummy.ndeallocs++;
    if(director.dummy.ndeallocs == director.dummy.nallocs){
        director.dummy.pos = 0;
    }
}

void interposer_setShadowIsLoaded() {
    director.shadowIsLoaded = 1;
}

static void _interposer_globalInitializeHelper() {
    if(directorIsInitialized) {
        return;
    }
    memset(&director, 0, sizeof(FuncDirector));

    /* use dummy malloc during initial dlsym calls to avoid recursive stack segfaults */
    director.next.malloc = dummy_malloc;
    director.next.calloc = dummy_calloc;
    director.next.free = dummy_free;

    malloc_func tempMalloc;
    calloc_func tempCalloc;
    free_func tempFree;

    SETSYM_OR_FAIL(tempMalloc, "malloc");
    SETSYM_OR_FAIL(tempCalloc, "calloc");
    SETSYM_OR_FAIL(tempFree, "free");

    /* stop using the dummy malloc funcs now */
    director.next.malloc = tempMalloc;
    director.next.calloc = tempCalloc;
    director.next.free = tempFree;

    /* lookup the remaining functions */
    SETSYM_OR_FAIL(director.next.realloc, "realloc");
    SETSYM_OR_FAIL(director.next.posix_memalign, "posix_memalign");
    SETSYM_OR_FAIL(director.next.memalign, "memalign");
    SETSYM_OR_FAIL(director.next.valloc, "valloc");
    SETSYM_OR_FAIL(director.next.pvalloc, "pvalloc");
    SETSYM_OR_FAIL(director.next.mmap, "mmap");
    SETSYM_OR_FAIL(director.next.epoll_create, "epoll_create");
    SETSYM_OR_FAIL(director.next.epoll_create1, "epoll_create1");
    SETSYM_OR_FAIL(director.next.epoll_ctl, "epoll_ctl");
    SETSYM_OR_FAIL(director.next.epoll_wait, "epoll_wait");
    SETSYM_OR_FAIL(director.next.epoll_pwait, "epoll_pwait");
    SETSYM_OR_FAIL(director.next.eventfd, "eventfd");
    SETSYM_OR_FAIL(director.next.timerfd_create, "timerfd_create");
    SETSYM_OR_FAIL(director.next.timerfd_settime, "timerfd_settime");
    SETSYM_OR_FAIL(director.next.timerfd_gettime, "timerfd_gettime");
    SETSYM_OR_FAIL(director.next.socket, "socket");
    SETSYM_OR_FAIL(director.next.socketpair, "socketpair");
    SETSYM_OR_FAIL(director.next.bind, "bind");
    SETSYM_OR_FAIL(director.next.getsockname, "getsockname");
    SETSYM_OR_FAIL(director.next.connect, "connect");
    SETSYM_OR_FAIL(director.next.getpeername, "getpeername");
    SETSYM_OR_FAIL(director.next.send, "send");
    SETSYM_OR_FAIL(director.next.sendto, "sendto");
    SETSYM_OR_FAIL(director.next.sendmsg, "sendmsg");
    SETSYM_OR_FAIL(director.next.recv, "recv");
    SETSYM_OR_FAIL(director.next.recvfrom, "recvfrom");
    SETSYM_OR_FAIL(director.next.recvmsg, "recvmsg");
    SETSYM_OR_FAIL(director.next.getsockopt, "getsockopt");
    SETSYM_OR_FAIL(director.next.setsockopt, "setsockopt");
    SETSYM_OR_FAIL(director.next.listen, "listen");
    SETSYM_OR_FAIL(director.next.accept, "accept");
    SETSYM_OR_FAIL(director.next.accept4, "accept4");
    SETSYM_OR_FAIL(director.next.shutdown, "shutdown");
    SETSYM_OR_FAIL(director.next.pipe, "pipe");
    SETSYM_OR_FAIL(director.next.pipe2, "pipe2");
    SETSYM_OR_FAIL(director.next.read, "read");
    SETSYM_OR_FAIL(director.next.write, "write");
    SETSYM_OR_FAIL(director.next.readv, "readv");
    SETSYM_OR_FAIL(director.next.writev, "writev");
    SETSYM_OR_FAIL(director.next.close, "close");
    SETSYM_OR_FAIL(director.next.fcntl, "fcntl");
    SETSYM_OR_FAIL(director.next.ioctl, "ioctl");
    SETSYM_OR_FAIL(director.next.getifaddrs, "getifaddrs");
    SETSYM_OR_FAIL(director.next.freeifaddrs, "freeifaddrs");
    SETSYM_OR_FAIL(director.next.sleep, "sleep");
    SETSYM_OR_FAIL(director.next.nanosleep, "nanosleep");
    SETSYM_OR_FAIL(director.next.usleep, "usleep");
    SETSYM_OR_FAIL(director.next.select, "select");
    SETSYM_OR_FAIL(director.next.pselect, "pselect");
    SETSYM_OR_FAIL(director.next.poll, "poll");
    SETSYM_OR_FAIL(director.next.ppoll, "ppoll");
    SETSYM_OR_FAIL(director.next.system, "system");
    SETSYM_OR_FAIL(director.next.fork, "fork");
    SETSYM_OR_FAIL(director.next.sigwait, "sigwait");
    SETSYM_OR_FAIL(director.next.waitpid, "waitpid");
    SETSYM_OR_FAIL(director.next.fileno, "fileno");
    SETSYM_OR_FAIL(director.next.open, "open");
    SETSYM_OR_FAIL(director.next.open64, "open64");
    SETSYM_OR_FAIL(director.next.creat, "creat");
    SETSYM_OR_FAIL(director.next.fopen, "fopen");
    SETSYM_OR_FAIL(director.next.fopen64, "fopen64");
    SETSYM_OR_FAIL(director.next.fdopen, "fdopen");
    SETSYM_OR_FAIL(director.next.dup, "dup");
    SETSYM_OR_FAIL(director.next.dup2, "dup2");
    SETSYM_OR_FAIL(director.next.dup3, "dup3");
    SETSYM_OR_FAIL(director.next.fclose, "fclose");
    SETSYM_OR_FAIL(director.next.__fxstat, "__fxstat");
    SETSYM_OR_FAIL(director.next.__fxstat64, "__fxstat64");
    SETSYM_OR_FAIL(director.next.fstatfs, "fstatfs");
    SETSYM_OR_FAIL(director.next.lseek, "lseek");
    SETSYM_OR_FAIL(director.next.lseek64, "lseek64");
    SETSYM_OR_FAIL(director.next.pread, "pread");
    SETSYM_OR_FAIL(director.next.pwrite, "pwrite");
    SETSYM_OR_FAIL(director.next.flock, "flock");
    SETSYM_OR_FAIL(director.next.fsync, "fsync");
    SETSYM_OR_FAIL(director.next.ftruncate, "ftruncate");
    SETSYM_OR_FAIL(director.next.ftruncate64, "ftruncate64");
    SETSYM_OR_FAIL(director.next.posix_fallocate, "posix_fallocate");
    SETSYM_OR_FAIL(director.next.fstatvfs, "fstatvfs");
    SETSYM_OR_FAIL(director.next.fdatasync, "fdatasync");
    SETSYM_OR_FAIL(director.next.syncfs, "syncfs");
    SETSYM_OR_FAIL(director.next.fallocate, "fallocate");
    SETSYM_OR_FAIL(director.next.fexecve, "fexecve");
    SETSYM_OR_FAIL(director.next.fpathconf, "fpathconf");
    SETSYM_OR_FAIL(director.next.fchdir, "fchdir");
    SETSYM_OR_FAIL(director.next.fchown, "fchown");
    SETSYM_OR_FAIL(director.next.fchmod, "fchmod");
    SETSYM_OR_FAIL(director.next.posix_fadvise, "posix_fadvise");
    SETSYM_OR_FAIL(director.next.lockf, "lockf");
    SETSYM_OR_FAIL(director.next.openat, "openat");
    SETSYM_OR_FAIL(director.next.faccessat, "faccessat");
    SETSYM_OR_FAIL(director.next.unlinkat, "unlinkat");
    SETSYM_OR_FAIL(director.next.fchmodat, "fchmodat");
    SETSYM_OR_FAIL(director.next.fchownat, "fchownat");

    SETSYM_OR_FAIL(director.next.fread, "fread");
    SETSYM_OR_FAIL(director.next.fwrite, "fwrite");
    SETSYM_OR_FAIL(director.next.fputc, "fputc");
    SETSYM_OR_FAIL(director.next.fputs, "fputs");
    SETSYM_OR_FAIL(director.next.putchar, "putchar");
    SETSYM_OR_FAIL(director.next.puts, "puts");
    SETSYM_OR_FAIL(director.next.printf, "printf");
    SETSYM_OR_FAIL(director.next.vprintf, "vprintf");
    SETSYM_OR_FAIL(director.next.fprintf, "fprintf");
    SETSYM_OR_FAIL(director.next.vfprintf, "vfprintf");
    SETSYM_OR_FAIL(director.next.fflush, "fflush");

    SETSYM_OR_FAIL(director.next.time, "time");
    SETSYM_OR_FAIL(director.next.clock_gettime, "clock_gettime");
    SETSYM_OR_FAIL(director.next.gettimeofday, "gettimeofday");
    SETSYM_OR_FAIL(director.next.localtime, "localtime");
    SETSYM_OR_FAIL(director.next.localtime_r, "localtime_r");

    SETSYM_OR_FAIL(director.next.gethostname, "gethostname");
    SETSYM_OR_FAIL(director.next.getaddrinfo, "getaddrinfo");
    SETSYM_OR_FAIL(director.next.freeaddrinfo, "freeaddrinfo");
    SETSYM_OR_FAIL(director.next.getnameinfo, "getnameinfo");
    SETSYM_OR_FAIL(director.next.gethostbyname, "gethostbyname");
    SETSYM_OR_FAIL(director.next.gethostbyname_r, "gethostbyname_r");
    SETSYM_OR_FAIL(director.next.gethostbyname2, "gethostbyname2");
    SETSYM_OR_FAIL(director.next.gethostbyname2_r, "gethostbyname2_r");
    SETSYM_OR_FAIL(director.next.gethostbyaddr, "gethostbyaddr");
    SETSYM_OR_FAIL(director.next.gethostbyaddr_r, "gethostbyaddr_r");
    SETSYM_OR_FAIL(director.next.rand, "rand");
    SETSYM_OR_FAIL(director.next.rand_r, "rand_r");
    SETSYM_OR_FAIL(director.next.srand, "srand");
    SETSYM_OR_FAIL(director.next.random, "random");
    SETSYM_OR_FAIL(director.next.random_r, "random_r");
    SETSYM_OR_FAIL(director.next.srandom, "srandom");
    SETSYM_OR_FAIL(director.next.srandom_r, "srandom_r");
    SETSYM_OR_FAIL(director.next.exit, "exit");
    SETSYM_OR_FAIL(director.next.on_exit, "on_exit");
    SETSYM_OR_FAIL(director.next.__cxa_atexit, "__cxa_atexit");

    /* pthread */
    SETSYM_OR_FAIL(director.next.pthread_attr_init, "pthread_attr_init");
    SETSYM_OR_FAIL(director.next.pthread_attr_destroy, "pthread_attr_destroy");
    SETSYM_OR_FAIL(director.next.pthread_attr_setinheritsched, "pthread_attr_setinheritsched");
    SETSYM_OR_FAIL(director.next.pthread_attr_getinheritsched, "pthread_attr_getinheritsched");
    SETSYM_OR_FAIL(director.next.pthread_attr_setschedparam, "pthread_attr_setschedparam");
    SETSYM_OR_FAIL(director.next.pthread_attr_getschedparam, "pthread_attr_getschedparam");
    SETSYM_OR_FAIL(director.next.pthread_attr_setschedpolicy, "pthread_attr_setschedpolicy");
    SETSYM_OR_FAIL(director.next.pthread_attr_getschedpolicy, "pthread_attr_getschedpolicy");
    SETSYM_OR_FAIL(director.next.pthread_attr_setscope, "pthread_attr_setscope");
    SETSYM_OR_FAIL(director.next.pthread_attr_getscope, "pthread_attr_getscope");
    SETSYM_OR_FAIL(director.next.pthread_attr_setstacksize, "pthread_attr_setstacksize");
    SETSYM_OR_FAIL(director.next.pthread_attr_getstacksize, "pthread_attr_getstacksize");
    SETSYM_OR_FAIL(director.next.pthread_attr_setstackaddr, "pthread_attr_setstackaddr");
    SETSYM_OR_FAIL(director.next.pthread_attr_getstackaddr, "pthread_attr_getstackaddr");
    SETSYM_OR_FAIL(director.next.pthread_attr_setdetachstate, "pthread_attr_setdetachstate");
    SETSYM_OR_FAIL(director.next.pthread_attr_getdetachstate, "pthread_attr_getdetachstate");
    SETSYM_OR_FAIL(director.next.pthread_attr_setguardsize, "pthread_attr_setguardsize");
    SETSYM_OR_FAIL(director.next.pthread_attr_getguardsize, "pthread_attr_getguardsize");
    SETSYM_OR_FAIL(director.next.pthread_create, "pthread_create");
    SETSYM_OR_FAIL(director.next.pthread_detach, "pthread_detach");
    SETSYM_OR_FAIL(director.next.pthread_self, "pthread_self");
    SETSYM_OR_FAIL(director.next.pthread_equal, "pthread_equal");
    SETSYM_OR_FAIL(director.next.pthread_yield, "pthread_yield");
    SETSYM_OR_FAIL(director.next.pthread_exit, "pthread_exit");
    SETSYM_OR_FAIL(director.next.pthread_join, "pthread_join");
    SETSYM_OR_FAIL(director.next.pthread_once, "pthread_once");
    SETSYM_OR_FAIL(director.next.pthread_sigmask, "pthread_sigmask");
    SETSYM_OR_FAIL(director.next.pthread_kill, "pthread_kill");
    SETSYM_OR_FAIL(director.next.pthread_getconcurrency, "pthread_getconcurrency");
    SETSYM_OR_FAIL(director.next.pthread_setconcurrency, "pthread_setconcurrency");
    SETSYM_OR_FAIL(director.next.pthread_key_create, "pthread_key_create");
    SETSYM_OR_FAIL(director.next.pthread_key_delete, "pthread_key_delete");
    SETSYM_OR_FAIL(director.next.pthread_setspecific, "pthread_setspecific");
    SETSYM_OR_FAIL(director.next.pthread_getspecific, "pthread_getspecific");
    SETSYM_OR_FAIL(director.next.pthread_cancel, "pthread_cancel");
    SETSYM_OR_FAIL(director.next.pthread_testcancel, "pthread_testcancel");
    SETSYM_OR_FAIL(director.next.pthread_setcancelstate, "pthread_setcancelstate");
    SETSYM_OR_FAIL(director.next.pthread_setcanceltype, "pthread_setcanceltype");
    SETSYM_OR_FAIL(director.next.pthread_setschedparam, "pthread_setschedparam");
    SETSYM_OR_FAIL(director.next.pthread_getschedparam, "pthread_getschedparam");
    SETSYM_OR_FAIL(director.next.pthread_atfork, "pthread_atfork");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_init, "pthread_mutexattr_init");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_destroy, "pthread_mutexattr_destroy");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_setprioceiling, "pthread_mutexattr_setprioceiling");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_getprioceiling, "pthread_mutexattr_getprioceiling");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_setprotocol, "pthread_mutexattr_setprotocol");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_getprotocol, "pthread_mutexattr_getprotocol");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_setpshared, "pthread_mutexattr_setpshared");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_getpshared, "pthread_mutexattr_getpshared");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_settype, "pthread_mutexattr_settype");
    SETSYM_OR_FAIL(director.next.pthread_mutexattr_gettype, "pthread_mutexattr_gettype");
    SETSYM_OR_FAIL(director.next.pthread_mutex_init, "pthread_mutex_init");
    SETSYM_OR_FAIL(director.next.pthread_mutex_destroy, "pthread_mutex_destroy");
    SETSYM_OR_FAIL(director.next.pthread_mutex_setprioceiling, "pthread_mutex_setprioceiling");
    SETSYM_OR_FAIL(director.next.pthread_mutex_getprioceiling, "pthread_mutex_getprioceiling");
    SETSYM_OR_FAIL(director.next.pthread_mutex_lock, "pthread_mutex_lock");
    SETSYM_OR_FAIL(director.next.pthread_mutex_trylock, "pthread_mutex_trylock");
    SETSYM_OR_FAIL(director.next.pthread_mutex_unlock, "pthread_mutex_unlock");
    SETSYM_OR_FAIL(director.next.pthread_rwlockattr_init, "pthread_rwlockattr_init");
    SETSYM_OR_FAIL(director.next.pthread_rwlockattr_destroy, "pthread_rwlockattr_destroy");
    SETSYM_OR_FAIL(director.next.pthread_rwlockattr_setpshared, "pthread_rwlockattr_setpshared");
    SETSYM_OR_FAIL(director.next.pthread_rwlockattr_getpshared, "pthread_rwlockattr_getpshared");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_init, "pthread_rwlock_init");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_destroy, "pthread_rwlock_destroy");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_rdlock, "pthread_rwlock_rdlock");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_tryrdlock, "pthread_rwlock_tryrdlock");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_wrlock, "pthread_rwlock_wrlock");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_trywrlock, "pthread_rwlock_trywrlock");
    SETSYM_OR_FAIL(director.next.pthread_rwlock_unlock, "pthread_rwlock_unlock");
    SETSYM_OR_FAIL(director.next.pthread_condattr_init, "pthread_condattr_init");
    SETSYM_OR_FAIL(director.next.pthread_condattr_destroy, "pthread_condattr_destroy");
    SETSYM_OR_FAIL(director.next.pthread_condattr_setpshared, "pthread_condattr_setpshared");
    SETSYM_OR_FAIL(director.next.pthread_condattr_getpshared, "pthread_condattr_getpshared");
    SETSYM_OR_FAIL(director.next.pthread_condattr_setclock, "pthread_condattr_setclock");
    SETSYM_OR_FAIL(director.next.pthread_condattr_getclock, "pthread_condattr_getclock");
    SETSYM_OR_FAIL(director.next.pthread_cond_init, "pthread_cond_init");
    SETSYM_OR_FAIL(director.next.pthread_cond_destroy, "pthread_cond_destroy");
    SETSYM_OR_FAIL(director.next.pthread_cond_broadcast, "pthread_cond_broadcast");
    SETSYM_OR_FAIL(director.next.pthread_cond_signal, "pthread_cond_signal");
    SETSYM_OR_FAIL(director.next.pthread_cond_wait, "pthread_cond_wait");
    SETSYM_OR_FAIL(director.next.pthread_cond_timedwait, "pthread_cond_timedwait");


    /* attempt lookup but don't fail as its valid not to exist */
    SETSYM(director.next.atexit, "atexit");
    SETSYM(director.next.aligned_alloc, "aligned_alloc");

    SETSYM(director.next.pthread_attr_setname_np, "pthread_attr_setname_np");
    SETSYM(director.next.pthread_attr_getname_np, "pthread_attr_getname_np");
    SETSYM(director.next.pthread_attr_setprio_np, "pthread_attr_setprio_np");
    SETSYM(director.next.pthread_attr_getprio_np, "pthread_attr_getprio_np");
    SETSYM(director.next.pthread_abort, "pthread_abort");
    SETSYM(director.next.__pthread_detach, "__pthread_detach");
    SETSYM(director.next.pthread_yield_np, "pthread_yield_np");
    SETSYM(director.next.pthread_cleanup_push, "pthread_cleanup_push");
    SETSYM(director.next.pthread_cleanup_pop, "pthread_cleanup_pop");

    directorIsInitialized = 1;
}

static void _interposer_globalInitialize() {
    /* ensure we recursively intercept during initialization */
    if(!__sync_fetch_and_add(&isRecursive, 1)){
        _interposer_globalInitializeHelper();
    }
    __sync_fetch_and_sub(&isRecursive, 1);
}

/* this function is called when the library is loaded,
 * and only once per process not once per thread */
void __attribute__((constructor)) construct() {
    /* here we are guaranteed no threads have started yet */
    _interposer_globalInitialize();
}

/* this function is called when the library is unloaded,
 * and only once per process not once per thread */
//void __attribute__((destructor)) destruct() {}

/****************************************************************************
 * Interposes switches execution control between Shadow, the plug-in program
 * and the process threading library (pth)
 ****************************************************************************/

static inline Process* _doEmulate() {
    if(!directorIsInitialized) {
        _interposer_globalInitialize();
    }
    Process* proc = NULL;
    /* recursive calls always go to libc */
    if(!__sync_fetch_and_add(&isRecursive, 1)) {
        proc = director.shadowIsLoaded && (*(&disableCount)) <= 0 && worker_isAlive() ? worker_getActiveProcess() : NULL;
        /* check if the shadow intercept library is loaded yet, but dont fail if its not */
        if(proc) {
            /* ask shadow if this call is a plug-in that should be intercepted */
            proc = process_shouldEmulate(proc) ? proc : NULL;
        } else {
            /* intercept library is not yet loaded, don't redirect */
            proc = NULL;
        }
    }
    __sync_fetch_and_sub(&isRecursive, 1);
    return proc;
}

/****************************************************************************
 * Preloaded functions that switch execution control from the plug-in program
 * back to Shadow
 ****************************************************************************/

/* functions that must be handled without macro */

/* calloc is special because its called during library initialization */
void* calloc(size_t nmemb, size_t size) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        return process_emu_calloc(proc, nmemb, size);
    } else {
        /* the dlsym lookup for calloc may call calloc again, causing infinite recursion */
        if(!director.next.calloc) {
            /* make sure to use dummy_calloc when looking up calloc */
            director.next.calloc = dummy_calloc;
            /* this will set director.real.calloc to the correct calloc */
            ENSURE(calloc);
        }
        return director.next.calloc(nmemb, size);
    }
}
/* free is special because of our dummy allocator used during initialization */
void free(void *ptr) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        process_emu_free(proc, ptr);
    } else {
        /* check if the ptr is in the dummy buf, and free it using the dummy free func */
        void* dummyBufStart = &(director.dummy.buf[0]);
        void* dummyBufEnd = &(director.dummy.buf[sizeof(director.dummy.buf)-1]);

        if (ptr >= dummyBufStart && ptr <= dummyBufEnd) {
            dummy_free(ptr);
            return;
        }

        ENSURE(free);
        director.next.free(ptr);
    }
}

/* use variable args */

int fcntl(int fd, int cmd, ...) {
    va_list farg;
    va_start(farg, cmd);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_fcntl(proc, fd, cmd, va_arg(farg, void*));
    } else {
        ENSURE(fcntl);
        result = director.next.fcntl(fd, cmd, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}

int ioctl(int fd, unsigned long int request, ...) {
    va_list farg;
    va_start(farg, request);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_ioctl(proc, fd, request, va_arg(farg, void*));
    } else {
        ENSURE(ioctl);
        result = director.next.ioctl(fd, request, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}

int open(const char *pathname, int flags, ...) {
    va_list farg;
    va_start(farg, flags);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_open(proc, pathname, flags, va_arg(farg, mode_t));
    } else {
        ENSURE(open);
        result = director.next.open(pathname, flags, va_arg(farg, mode_t));
    }
    va_end(farg);
    return result;
}

int open64(const char *pathname, int flags, ...) {
    va_list farg;
    va_start(farg, flags);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_open64(proc, pathname, flags, va_arg(farg, mode_t));
    } else {
        ENSURE(open64);
        result = director.next.open64(pathname, flags, va_arg(farg, mode_t));
    }
    va_end(farg);
    return result;
}

int openat(int dirfd, const char *pathname, int flags, ...) {
    va_list farg;
    va_start(farg, flags);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_openat(proc, dirfd, pathname, flags, va_arg(farg, mode_t));
    } else {
        ENSURE(openat);
        result = director.next.openat(dirfd, pathname, flags, va_arg(farg, mode_t));
    }
    va_end(farg);
    return result;
}

int printf(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_vprintf(proc, format, arglist);
    } else {
        ENSURE(vprintf);
        result = director.next.vprintf(format, arglist);
    }
    va_end(arglist);
    return result;
}

int fprintf(FILE *stream, const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_vfprintf(proc, stream, format, arglist);
    } else {
        ENSURE(vfprintf);
        result = director.next.vfprintf(stream, format, arglist);
    }
    va_end(arglist);
    return result;
}

/* memory allocation family */

INTERPOSE(void* malloc(size_t a), malloc, a);
INTERPOSE(void* realloc(void* a, size_t b), realloc, a, b);
INTERPOSE(int posix_memalign(void** a, size_t b, size_t c), posix_memalign, a, b, c);
INTERPOSE(void* memalign(size_t a, size_t b), memalign, a, b);
INTERPOSE(void* aligned_alloc(size_t a, size_t b), aligned_alloc, a, b);
INTERPOSE(void* valloc(size_t a), valloc, a);
INTERPOSE(void* pvalloc(size_t a), pvalloc, a);
INTERPOSE(void* mmap(void *a, size_t b, int c, int d, int e, off_t f), mmap, a, b, c, d, e, f);

/* event family */

INTERPOSE(int epoll_create(int a), epoll_create, a);
INTERPOSE(int epoll_create1(int a), epoll_create1, a);
INTERPOSE(int epoll_ctl(int a, int b, int c, struct epoll_event* d), epoll_ctl, a, b, c, d);
INTERPOSE(int epoll_wait(int a, struct epoll_event* b, int c, int d), epoll_wait, a, b, c, d);
INTERPOSE(int epoll_pwait(int a, struct epoll_event* b, int c, int d, const sigset_t* e), epoll_pwait, a, b, c, d, e);

/* socket/io family */

INTERPOSE(int socket(int a, int b, int c), socket, a, b, c);
INTERPOSE(int socketpair(int a, int b, int c, int d[2]), socketpair, a, b, c, d);
INTERPOSE(int bind(int a, const struct sockaddr* b, socklen_t c), bind, a, b, c);
INTERPOSE(int getsockname(int a, struct sockaddr* b, socklen_t* c), getsockname, a, b, c);
INTERPOSE(int connect(int a, const struct sockaddr* b, socklen_t c), connect, a, b, c);
INTERPOSE(int getpeername(int a, struct sockaddr* b, socklen_t* c), getpeername, a, b, c);
INTERPOSE(ssize_t send(int a, const void *b, size_t c, int d), send, a, b, c, d);
INTERPOSE(ssize_t sendto(int a, const void *b, size_t c, int d, const struct sockaddr* e, socklen_t f), sendto, a, b, c, d, e, f);
INTERPOSE(ssize_t sendmsg(int a, const struct msghdr *b, int c), sendmsg, a, b, c);
INTERPOSE(ssize_t recv(int a, void *b, size_t c, int d), recv, a, b, c, d);
INTERPOSE(ssize_t recvfrom(int a, void *b, size_t c, int d, struct sockaddr* e, socklen_t *restrict f), recvfrom, a, b, c, d, e, f);
INTERPOSE(ssize_t recvmsg(int a, struct msghdr *b, int c), recvmsg, a, b, c);
INTERPOSE(int getsockopt(int a, int b, int c, void* d, socklen_t* e), getsockopt, a, b, c, d, e);
INTERPOSE(int setsockopt(int a, int b, int c, const void *d, socklen_t e), setsockopt, a, b, c, d, e);
INTERPOSE(int listen(int a, int b), listen, a, b);
INTERPOSE(int accept(int a, struct sockaddr* b, socklen_t* c), accept, a, b, c);
INTERPOSE(int accept4(int a, struct sockaddr* b, socklen_t* c, int d), accept4, a, b, c, d);
INTERPOSE(int shutdown(int a, int b), shutdown, a, b);
INTERPOSE(ssize_t read(int a, void *b, size_t c), read, a, b, c);
INTERPOSE(ssize_t write(int a, const void *b, size_t c), write, a, b, c);
INTERPOSE(ssize_t readv(int a, const struct iovec *b, int c), readv, a, b, c);
INTERPOSE(ssize_t writev(int a, const struct iovec *b, int c), writev, a, b, c);
INTERPOSE(ssize_t pread(int a, void *b, size_t c, off_t d), pread, a, b, c, d);
INTERPOSE(ssize_t pwrite(int a, const void *b, size_t c, off_t d), pwrite, a, b, c, d);
INTERPOSE(int close(int a), close, a);
INTERPOSE(int pipe2(int a[2], int b), pipe2, a, b);
INTERPOSE(int pipe(int a[2]), pipe, a);
INTERPOSE(int getifaddrs(struct ifaddrs **a), getifaddrs, a);
INTERPOSE_NORET(void freeifaddrs(struct ifaddrs *a), freeifaddrs, a);

/* polling */

INTERPOSE(unsigned int sleep(unsigned int a), sleep, a);
INTERPOSE(int nanosleep(const struct timespec *a, struct timespec *b), nanosleep, a, b);
INTERPOSE(int usleep(unsigned int a), usleep, a);
INTERPOSE(int select(int a, fd_set *b, fd_set *c, fd_set *d, struct timeval *e), select, a, b, c, d, e);
INTERPOSE(int pselect(int a, fd_set *b, fd_set *c, fd_set *d, const struct timespec *e, const sigset_t *f), pselect, a, b, c, d, e, f);
INTERPOSE(int poll(struct pollfd *a, nfds_t b, int c), poll, a, b, c);
INTERPOSE(int ppoll(struct pollfd *a, nfds_t b, const struct timespec* c, const sigset_t* d), ppoll, a, b, c, d);
INTERPOSE(int system(const char *a), system, a);
INTERPOSE(pid_t fork(void), fork);
INTERPOSE(pid_t waitpid(pid_t a, int *b, int c), waitpid, a, b, c);
INTERPOSE(int sigwait(const sigset_t *a, int *b), sigwait, a, b);

/* timers */

INTERPOSE(int eventfd(int a, int b), eventfd, a, b);
INTERPOSE(int timerfd_create(int a, int b), timerfd_create, a, b);
INTERPOSE(int timerfd_settime(int a, int b, const struct itimerspec *c, struct itimerspec *d), timerfd_settime, a, b, c, d);
INTERPOSE(int timerfd_gettime(int a, struct itimerspec *b), timerfd_gettime, a, b);

/* file specific */

INTERPOSE(int fileno(FILE *a), fileno, a);
INTERPOSE(int creat(const char *a, mode_t b), creat, a, b);
INTERPOSE(FILE *fopen(const char *a, const char *b), fopen, a, b);
INTERPOSE(FILE *fopen64(const char *a, const char *b), fopen64, a, b);
INTERPOSE(FILE *fdopen(int a, const char *b), fdopen, a, b);
INTERPOSE(int dup(int a), dup, a);
INTERPOSE(int dup2(int a, int b), dup2, a, b);
INTERPOSE(int dup3(int a, int b, int c), dup3, a, b, c);
INTERPOSE(int fclose(FILE *a), fclose, a);
/* fstat redirects to this */
INTERPOSE(int __fxstat (int a, int b, struct stat *c), __fxstat, a, b, c);
/* fstat64 redirects to this */
INTERPOSE(int __fxstat64 (int a, int b, struct stat64 *c), __fxstat64, a, b, c);
INTERPOSE(int fstatfs (int a, struct statfs *b), fstatfs, a, b);
INTERPOSE(int fstatfs64 (int a, struct statfs64 *b), fstatfs64, a, b);
INTERPOSE(off_t lseek(int a, off_t b, int c), lseek, a, b, c);
INTERPOSE(off64_t lseek64(int a, off64_t b, int c), lseek64, a, b, c);
INTERPOSE(int flock(int a, int b), flock, a, b);
INTERPOSE(int fsync(int a), fsync, a);
INTERPOSE(int ftruncate(int a, off_t b), ftruncate, a, b);
INTERPOSE(int ftruncate64(int a, off64_t b), ftruncate64, a, b);
INTERPOSE(int posix_fallocate(int a, off_t b, off_t c), posix_fallocate, a, b, c);
INTERPOSE(int fstatvfs(int a, struct statvfs *b), fstatvfs, a, b);
INTERPOSE(int fdatasync(int a), fdatasync, a);
INTERPOSE(int syncfs(int a), syncfs, a);
INTERPOSE(int fallocate(int a, int b, off_t c, off_t d), fallocate, a, b, c, d);
INTERPOSE(int fexecve(int a, char *const b[], char *const c[]), fexecve, a, b, c);
INTERPOSE(long fpathconf(int a, int b), fpathconf, a, b);
INTERPOSE(int fchdir(int a), fchdir, a);
INTERPOSE(int fchown(int a, uid_t b, gid_t c), fchown, a, b, c);
INTERPOSE(int fchmod(int a, mode_t b), fchmod, a, b);
INTERPOSE(int posix_fadvise(int a, off_t b, off_t c, int d), posix_fadvise, a, b, c, d);
INTERPOSE(int lockf(int a, int b, off_t c), lockf, a, b, c);
INTERPOSE(int faccessat(int a, const char *b, int c, int d), faccessat, a, b, c, d);
INTERPOSE(int unlinkat(int a, const char *b, int c), unlinkat, a, b, c);
INTERPOSE(int fchmodat(int a, const char *b, mode_t c, int d), fchmodat, a, b, c, d);
INTERPOSE(int fchownat(int a, const char *b, uid_t c, gid_t d, int e), fchownat, a, b, c, d, e);

INTERPOSE(size_t fread(void *a, size_t b, size_t c, FILE *d), fread, a, b, c, d);
INTERPOSE(size_t fwrite(const void *a, size_t b, size_t c, FILE *d), fwrite, a, b, c, d);
INTERPOSE(int fputc(int a, FILE *b), fputc, a, b);
INTERPOSE(int fputs(const char *a, FILE *b), fputs, a, b);
INTERPOSE(int putchar(int a), putchar, a);
INTERPOSE(int puts(const char *a), puts, a);
INTERPOSE(int vprintf(const char *a, va_list b), vprintf, a, b);
INTERPOSE(int vfprintf(FILE *a, const char *b, va_list c), vfprintf, a, b, c);
INTERPOSE(int fflush(FILE *a), fflush, a);

/* time family */

INTERPOSE(time_t time(time_t *a), time, a);
INTERPOSE(int clock_gettime(clockid_t a, struct timespec *b), clock_gettime, a, b);
INTERPOSE(int gettimeofday(struct timeval* a, struct timezone* b), gettimeofday, a, b);
INTERPOSE(struct tm *localtime(const time_t *a), localtime, a);
INTERPOSE(struct tm *localtime_r(const time_t *a, struct tm *b), localtime_r, a, b);

/* name/address family */

/* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
INTERPOSE(int getnameinfo(const struct sockaddr* a, socklen_t b, char * c, socklen_t d, char *e, socklen_t f, int g), getnameinfo, a, b, c, d, e, f, g);
#else
INTERPOSE(int getnameinfo(const struct sockaddr* a, socklen_t b, char * c, socklen_t d, char *e, socklen_t f, unsigned int g), getnameinfo, a, b, c, d, e, f, g);
#endif
INTERPOSE(int gethostname(char* a, size_t b), gethostname, a, b);
INTERPOSE(int getaddrinfo(const char *a, const char *b, const struct addrinfo *c, struct addrinfo **d), getaddrinfo, a, b, c, d);
INTERPOSE_NORET(void freeaddrinfo(struct addrinfo *a), freeaddrinfo, a);

INTERPOSE(struct hostent* gethostbyname(const char* a), gethostbyname, a);
INTERPOSE(int gethostbyname_r(const char *a, struct hostent *b, char *c, size_t d, struct hostent **e, int *f), gethostbyname_r, a, b, c, d, e, f);
INTERPOSE(struct hostent* gethostbyname2(const char* a, int b), gethostbyname2, a, b);
INTERPOSE(int gethostbyname2_r(const char *a, int b, struct hostent *c, char *d, size_t e, struct hostent **f, int *g), gethostbyname2_r, a, b, c, d, e, f, g);
INTERPOSE(struct hostent* gethostbyaddr(const void* a, socklen_t b, int c), gethostbyaddr, a, b, c);
INTERPOSE(int gethostbyaddr_r(const void *a, socklen_t b, int c, struct hostent *d, char *e, size_t f, struct hostent **g, int *h), gethostbyaddr_r, a, b, c, d, e, f, g, h);

/* random family */

INTERPOSE(int rand(), rand);
INTERPOSE(int rand_r(unsigned int *a), rand_r, a);
INTERPOSE_NORET(void srand(unsigned int a), srand, a);
INTERPOSE(long int random(), random);
INTERPOSE(int random_r(struct random_data *a, int32_t *b), random_r, a, b);
INTERPOSE_NORET(void srandom(unsigned int a), srandom, a);
INTERPOSE(int srandom_r(unsigned int a, struct random_data *b), srandom_r, a, b);

/* exit family */

INTERPOSE_NORET(void exit(int a), exit, a);
INTERPOSE(int on_exit(void (*a)(int , void *), void *b), on_exit, a, b);
INTERPOSE(int atexit(void (*a)(void)), atexit, a);
INTERPOSE(int __cxa_atexit(void (*a) (void *), void *b, void *c), __cxa_atexit, a, b, c);

/* pthread attributes */

INTERPOSE(int pthread_attr_init(pthread_attr_t *a), pthread_attr_init, a);
INTERPOSE(int pthread_attr_destroy(pthread_attr_t *a), pthread_attr_destroy, a);
INTERPOSE(int pthread_attr_setinheritsched(pthread_attr_t *a, int b), pthread_attr_setinheritsched, a, b);
INTERPOSE(int pthread_attr_getinheritsched(const pthread_attr_t *a, int *b), pthread_attr_getinheritsched, a, b);
INTERPOSE(int pthread_attr_setschedparam(pthread_attr_t *a, const struct sched_param *b), pthread_attr_setschedparam, a, b);
INTERPOSE(int pthread_attr_getschedparam(const pthread_attr_t *a, struct sched_param *b), pthread_attr_getschedparam, a, b);
INTERPOSE(int pthread_attr_setschedpolicy(pthread_attr_t *a, int b), pthread_attr_setschedpolicy, a, b);
INTERPOSE(int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *b), pthread_attr_getschedpolicy, a, b);
INTERPOSE(int pthread_attr_setscope(pthread_attr_t *a, int b), pthread_attr_setscope, a, b);
INTERPOSE(int pthread_attr_getscope(const pthread_attr_t *a, int *b), pthread_attr_getscope, a, b);
INTERPOSE(int pthread_attr_setstacksize(pthread_attr_t *a, size_t b), pthread_attr_setstacksize, a, b);
INTERPOSE(int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *b), pthread_attr_getstacksize, a, b);
INTERPOSE(int pthread_attr_setstackaddr(pthread_attr_t *a, void *b), pthread_attr_setstackaddr, a, b);
INTERPOSE(int pthread_attr_getstackaddr(const pthread_attr_t *a, void **b), pthread_attr_getstackaddr, a, b);
INTERPOSE(int pthread_attr_setdetachstate(pthread_attr_t *a, int b), pthread_attr_setdetachstate, a, b);
INTERPOSE(int pthread_attr_getdetachstate(const pthread_attr_t *a, int *b), pthread_attr_getdetachstate, a, b);
INTERPOSE(int pthread_attr_setguardsize(pthread_attr_t *a, size_t b), pthread_attr_setguardsize, a, b);
INTERPOSE(int pthread_attr_getguardsize(const pthread_attr_t *a, size_t *b), pthread_attr_getguardsize, a, b);
INTERPOSE(int pthread_attr_setname_np(pthread_attr_t *a, char *b), pthread_attr_setname_np, a, b);
INTERPOSE(int pthread_attr_getname_np(const pthread_attr_t *a, char **b), pthread_attr_getname_np, a, b);
INTERPOSE(int pthread_attr_setprio_np(pthread_attr_t *a, int b), pthread_attr_setprio_np, a, b);
INTERPOSE(int pthread_attr_getprio_np(const pthread_attr_t *a, int *b), pthread_attr_getprio_np, a, b);

/* pthread threads */

INTERPOSE(int pthread_create(pthread_t *a, const pthread_attr_t *b, void *(*c)(void *), void *d), pthread_create, a, b, c, d);
INTERPOSE(int pthread_detach(pthread_t a), pthread_detach, a);
INTERPOSE(int __pthread_detach(pthread_t a), __pthread_detach, a);
INTERPOSE(pthread_t pthread_self(void), pthread_self);
INTERPOSE(int pthread_equal(pthread_t a, pthread_t b), pthread_equal, a, b);
INTERPOSE(int pthread_yield(void), pthread_yield);
INTERPOSE(int pthread_yield_np(void), pthread_yield_np);
INTERPOSE_NORET(void pthread_exit(void *a), pthread_exit, a);
INTERPOSE(int pthread_join(pthread_t a, void **b), pthread_join, a, b);
INTERPOSE(int pthread_once(pthread_once_t *a, void (*b)(void)), pthread_once, a, b);
INTERPOSE(int pthread_sigmask(int a, const sigset_t *b, sigset_t *c), pthread_sigmask, a, b, c);
INTERPOSE(int pthread_kill(pthread_t a, int b), pthread_kill, a, b);
INTERPOSE(int pthread_abort(pthread_t a), pthread_abort, a);

/* concurrency */

INTERPOSE(int pthread_getconcurrency(void), pthread_getconcurrency);
INTERPOSE(int pthread_setconcurrency(int a), pthread_setconcurrency, a);

/* pthread context */

/* intercepting these functions causes glib errors, because keys that were created from
 * internal shadow functions then get used in the plugin and get forwarded to pth, which
 * of course does not have the same registered keys. */
//INTERPOSE(int pthread_key_create(pthread_key_t *a, void (*b)(void *)), pthread_key_create, a, b);
//INTERPOSE(int pthread_key_delete(pthread_key_t a), pthread_key_delete, a);
//INTERPOSE(int pthread_setspecific(pthread_key_t a, const void *b), pthread_setspecific, a, b);
//INTERPOSE(void* pthread_getspecific(pthread_key_t a), pthread_getspecific, a);

/* pthread cancel */

INTERPOSE(int pthread_cancel(pthread_t a), pthread_cancel, a);
INTERPOSE_NORET(void pthread_testcancel(void), pthread_testcancel);
INTERPOSE(int pthread_setcancelstate(int a, int *b), pthread_setcancelstate, a, b);
INTERPOSE(int pthread_setcanceltype(int a, int *b), pthread_setcanceltype, a, b);

/* pthread scheduler */

INTERPOSE(int pthread_setschedparam(pthread_t a, int b, const struct sched_param *c), pthread_setschedparam, a, b, c);
INTERPOSE(int pthread_getschedparam(pthread_t a, int *b, struct sched_param *c), pthread_getschedparam, a, b, c);

/* pthread cleanup */

//INTERPOSE_NORET(void pthread_cleanup_push(void (*a)(void *), void *b), pthread_cleanup_push, a, b);
//INTERPOSE_NORET(void pthread_cleanup_pop(int a), pthread_cleanup_pop, a, b);

/* forking */

INTERPOSE(int pthread_atfork(void (*a)(void), void (*b)(void), void (*c)(void)), pthread_atfork, a, b, c);

/* pthread mutex attributes */

INTERPOSE(int pthread_mutexattr_init(pthread_mutexattr_t *a), pthread_mutexattr_init, a);
INTERPOSE(int pthread_mutexattr_destroy(pthread_mutexattr_t *a), pthread_mutexattr_destroy, a);
INTERPOSE(int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *a, int b), pthread_mutexattr_setprioceiling, a, b);
INTERPOSE(int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *a, int *b), pthread_mutexattr_getprioceiling, a, b);
INTERPOSE(int pthread_mutexattr_setprotocol(pthread_mutexattr_t *a, int b), pthread_mutexattr_setprotocol, a, b);
INTERPOSE(int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *a, int *b), pthread_mutexattr_getprotocol, a, b);
INTERPOSE(int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int b), pthread_mutexattr_setpshared, a, b);
INTERPOSE(int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *b), pthread_mutexattr_getpshared, a, b);
INTERPOSE(int pthread_mutexattr_settype(pthread_mutexattr_t *a, int b), pthread_mutexattr_settype, a, b);
INTERPOSE(int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *b), pthread_mutexattr_gettype, a, b);

/* pthread mutex */

INTERPOSE(int pthread_mutex_init(pthread_mutex_t *a, const pthread_mutexattr_t *b), pthread_mutex_init, a, b);
INTERPOSE(int pthread_mutex_destroy(pthread_mutex_t *a), pthread_mutex_destroy, a);
INTERPOSE(int pthread_mutex_setprioceiling(pthread_mutex_t *a, int b, int *c), pthread_mutex_setprioceiling, a, b, c);
INTERPOSE(int pthread_mutex_getprioceiling(const pthread_mutex_t *a, int *b), pthread_mutex_getprioceiling, a, b);
INTERPOSE(int pthread_mutex_lock(pthread_mutex_t *a), pthread_mutex_lock, a);
INTERPOSE(int pthread_mutex_trylock(pthread_mutex_t *a), pthread_mutex_trylock, a);
INTERPOSE(int pthread_mutex_unlock(pthread_mutex_t *a), pthread_mutex_unlock, a);

/* pthread lock attributes */

INTERPOSE(int pthread_rwlockattr_init(pthread_rwlockattr_t *a), pthread_rwlockattr_init, a);
INTERPOSE(int pthread_rwlockattr_destroy(pthread_rwlockattr_t *a), pthread_rwlockattr_destroy, a);
INTERPOSE(int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *a, int b), pthread_rwlockattr_setpshared, a, b);
INTERPOSE(int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *a, int *b), pthread_rwlockattr_getpshared, a, b);

/* pthread locks */

INTERPOSE(int pthread_rwlock_init(pthread_rwlock_t *a, const pthread_rwlockattr_t *b), pthread_rwlock_init, a, b);
INTERPOSE(int pthread_rwlock_destroy(pthread_rwlock_t *a), pthread_rwlock_destroy, a);
INTERPOSE(int pthread_rwlock_rdlock(pthread_rwlock_t *a), pthread_rwlock_rdlock, a);
INTERPOSE(int pthread_rwlock_tryrdlock(pthread_rwlock_t *a), pthread_rwlock_tryrdlock, a);
INTERPOSE(int pthread_rwlock_wrlock(pthread_rwlock_t *a), pthread_rwlock_wrlock, a);
INTERPOSE(int pthread_rwlock_trywrlock(pthread_rwlock_t *a), pthread_rwlock_trywrlock, a);
INTERPOSE(int pthread_rwlock_unlock(pthread_rwlock_t *a), pthread_rwlock_unlock, a);

/* pthread condition attributes */

INTERPOSE(int pthread_condattr_init(pthread_condattr_t *a), pthread_condattr_init, a);
INTERPOSE(int pthread_condattr_destroy(pthread_condattr_t *a), pthread_condattr_destroy, a);
INTERPOSE(int pthread_condattr_setpshared(pthread_condattr_t *a, int b), pthread_condattr_setpshared, a, b);
INTERPOSE(int pthread_condattr_getpshared(const pthread_condattr_t *a, int *b), pthread_condattr_getpshared, a, b);
INTERPOSE(int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t b), pthread_condattr_setclock, a, b);
INTERPOSE(int pthread_condattr_getclock(const pthread_condattr_t *a, clockid_t *b), pthread_condattr_getclock, a, b);

/* pthread conditions */

INTERPOSE(int pthread_cond_init(pthread_cond_t *a, const pthread_condattr_t *b), pthread_cond_init, a, b);
INTERPOSE(int pthread_cond_destroy(pthread_cond_t *a), pthread_cond_destroy, a);
INTERPOSE(int pthread_cond_broadcast(pthread_cond_t *a), pthread_cond_broadcast, a);
INTERPOSE(int pthread_cond_signal(pthread_cond_t *a), pthread_cond_signal, a);
INTERPOSE(int pthread_cond_wait(pthread_cond_t *a, pthread_mutex_t *b), pthread_cond_wait, a, b);
INTERPOSE(int pthread_cond_timedwait(pthread_cond_t *a, pthread_mutex_t *b, const struct timespec *c), pthread_cond_timedwait, a, b, c);
