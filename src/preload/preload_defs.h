/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "preload/preload_includes.h"

/* memory allocation family */

PRELOADDEF(return, void*, realloc, (void* a, size_t b), a, b);
PRELOADDEF(return, int, posix_memalign, (void** a, size_t b, size_t c), a, b, c);
PRELOADDEF(return, void*, memalign, (size_t a, size_t b), a, b);
PRELOADDEF(return, void*, aligned_alloc, (size_t a, size_t b), a, b);
PRELOADDEF(return, void*, valloc, (size_t a), a);
PRELOADDEF(return, void*, pvalloc, (size_t a), a);
PRELOADDEF(return, void*, mmap, (void *a, size_t b, int c, int d, int e, off_t f), a, b, c, d, e, f);

/* event family */

PRELOADDEF(return, int, epoll_create, (int a), a);
PRELOADDEF(return, int, epoll_create1, (int a), a);
PRELOADDEF(return, int, epoll_ctl, (int a, int b, int c, struct epoll_event* d), a, b, c, d);
PRELOADDEF(return, int, epoll_wait, (int a, struct epoll_event* b, int c, int d), a, b, c, d);
PRELOADDEF(return, int, epoll_pwait, (int a, struct epoll_event* b, int c, int d, const sigset_t* e), a, b, c, d, e);

/* socket/io family */

PRELOADDEF(return, int, socket, (int a, int b, int c), a, b, c);
PRELOADDEF(return, int, socketpair, (int a, int b, int c, int d[2]), a, b, c, d);
PRELOADDEF(return, int, bind, (int a, const struct sockaddr* b, socklen_t c), a, b, c);
PRELOADDEF(return, int, getsockname, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
PRELOADDEF(return, int, connect, (int a, const struct sockaddr* b, socklen_t c), a, b, c);
PRELOADDEF(return, int, getpeername, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
PRELOADDEF(return, ssize_t, send, (int a, const void *b, size_t c, int d), a, b, c, d);
PRELOADDEF(return, ssize_t, sendto, (int a, const void *b, size_t c, int d, const struct sockaddr* e, socklen_t f), a, b, c, d, e, f);
PRELOADDEF(return, ssize_t, sendmsg, (int a, const struct msghdr *b, int c), a, b, c);
PRELOADDEF(return, ssize_t, recv, (int a, void *b, size_t c, int d), a, b, c, d);
PRELOADDEF(return, ssize_t, recvfrom, (int a, void *b, size_t c, int d, struct sockaddr* e, socklen_t *f), a, b, c, d, e, f);
PRELOADDEF(return, ssize_t, recvmsg, (int a, struct msghdr *b, int c), a, b, c);
PRELOADDEF(return, int, getsockopt, (int a, int b, int c, void* d, socklen_t* e), a, b, c, d, e);
PRELOADDEF(return, int, setsockopt, (int a, int b, int c, const void *d, socklen_t e), a, b, c, d, e);
PRELOADDEF(return, int, listen, (int a, int b), a, b);
PRELOADDEF(return, int, accept, (int a, struct sockaddr* b, socklen_t* c), a, b, c);
PRELOADDEF(return, int, accept4, (int a, struct sockaddr* b, socklen_t* c, int d), a, b, c, d);
PRELOADDEF(return, int, shutdown, (int a, int b), a, b);
PRELOADDEF(return, ssize_t, read, (int a, void *b, size_t c), a, b, c);
PRELOADDEF(return, ssize_t, write, (int a, const void *b, size_t c), a, b, c);
PRELOADDEF(return, ssize_t, readv, (int a, const struct iovec *b, int c), a, b, c);
PRELOADDEF(return, ssize_t, writev, (int a, const struct iovec *b, int c), a, b, c);
PRELOADDEF(return, ssize_t, pread, (int a, void *b, size_t c, off_t d), a, b, c, d);
PRELOADDEF(return, ssize_t, pwrite, (int a, const void *b, size_t c, off_t d), a, b, c, d);
PRELOADDEF(return, int, close, (int a), a);
PRELOADDEF(return, int, pipe2, (int a[2], int b), a, b);
PRELOADDEF(return, int, pipe, (int a[2]), a);
PRELOADDEF(return, int, getifaddrs, (struct ifaddrs **a), a);
PRELOADDEF(      , void, freeifaddrs, (struct ifaddrs *a), a);

/* polling */

PRELOADDEF(return, unsigned int, sleep, (unsigned int a), a);
PRELOADDEF(return, int, nanosleep, (const struct timespec *a, struct timespec *b), a, b);
PRELOADDEF(return, int, usleep, (unsigned int a), a);
PRELOADDEF(return, int, select, (int a, fd_set *b, fd_set *c, fd_set *d, struct timeval *e), a, b, c, d, e);
PRELOADDEF(return, int, pselect, (int a, fd_set *b, fd_set *c, fd_set *d, const struct timespec *e, const sigset_t *f), a, b, c, d, e, f);
PRELOADDEF(return, int, poll, (struct pollfd *a, nfds_t b, int c), a, b, c);
PRELOADDEF(return, int, ppoll, (struct pollfd *a, nfds_t b, const struct timespec* c, const sigset_t* d), a, b, c, d);
PRELOADDEF(return, int, system, (const char *a), a);
PRELOADDEF(return, pid_t, fork, (void));
PRELOADDEF(return, pid_t, waitpid, (pid_t a, int *b, int c), a, b, c);
PRELOADDEF(return, int, sigwait, (const sigset_t *a, int *b), a, b);

/* timers */

PRELOADDEF(return, int, eventfd, (int a, int b), a, b);
PRELOADDEF(return, int, timerfd_create, (int a, int b), a, b);
PRELOADDEF(return, int, timerfd_settime, (int a, int b, const struct itimerspec *c, struct itimerspec *d), a, b, c, d);
PRELOADDEF(return, int, timerfd_gettime, (int a, struct itimerspec *b), a, b);

/* file specific */

PRELOADDEF(return, int, fileno, (FILE *a), a);
PRELOADDEF(return, int, creat, (const char *a, mode_t b), a, b);
PRELOADDEF(return, FILE *, fmemopen, (void *a, size_t b, const char *c), a, b, c);
PRELOADDEF(return, FILE *, open_memstream, (char** a, size_t* b), a, b);
PRELOADDEF(return, FILE *, open_wmemstream, (wchar_t** a, size_t* b), a, b);
PRELOADDEF(return, FILE *, fopen, (const char *a, const char *b), a, b);
PRELOADDEF(return, FILE *, fopen64, (const char *a, const char *b), a, b);
PRELOADDEF(return, FILE *, fdopen, (int a, const char *b), a, b);
PRELOADDEF(return, int, dup, (int a), a);
PRELOADDEF(return, int, dup2, (int a, int b), a, b);
PRELOADDEF(return, int, dup3, (int a, int b, int c), a, b, c);
PRELOADDEF(return, int, fclose, (FILE *a), a);

PRELOADDEF(return, int, fseek, (FILE *a, long b, int c), a, b, c);
PRELOADDEF(return, long, ftell, (FILE *a), a);
PRELOADDEF(      , void, rewind, (FILE *a), a);
PRELOADDEF(return, int, fgetpos, (FILE *a, fpos_t *b), a, b);
PRELOADDEF(return, int, fsetpos, (FILE *a, const fpos_t *b), a, b);

/* fstat redirects to this */
PRELOADDEF(return, int, __fxstat, (int a, int b, struct stat *c), a, b, c);
/* fstat64 redirects to this */
PRELOADDEF(return, int, __fxstat64, (int a, int b, struct stat64 *c), a, b, c);
PRELOADDEF(return, int, fstatfs, (int a, struct statfs *b), a, b);
PRELOADDEF(return, int, fstatfs64, (int a, struct statfs64 *b), a, b);
PRELOADDEF(return, off_t, lseek, (int a, off_t b, int c), a, b, c);
PRELOADDEF(return, off64_t, lseek64, (int a, off64_t b, int c), a, b, c);
PRELOADDEF(return, int, flock, (int a, int b), a, b);
PRELOADDEF(return, int, fsync, (int a), a);
PRELOADDEF(return, int, ftruncate, (int a, off_t b), a, b);
PRELOADDEF(return, int, ftruncate64, (int a, off64_t b), a, b);
PRELOADDEF(return, int, posix_fallocate, (int a, off_t b, off_t c), a, b, c);
PRELOADDEF(return, int, fstatvfs, (int a, struct statvfs *b), a, b);
PRELOADDEF(return, int, fdatasync, (int a), a);
PRELOADDEF(return, int, syncfs, (int a), a);
PRELOADDEF(return, int, fallocate, (int a, int b, off_t c, off_t d), a, b, c, d);
PRELOADDEF(return, int, fexecve, (int a, char *const b[], char *const c[]), a, b, c);
PRELOADDEF(return, long, fpathconf, (int a, int b), a, b);
PRELOADDEF(return, int, fchdir, (int a), a);
PRELOADDEF(return, int, fchown, (int a, uid_t b, gid_t c), a, b, c);
PRELOADDEF(return, int, fchmod, (int a, mode_t b), a, b);
PRELOADDEF(return, int, posix_fadvise, (int a, off_t b, off_t c, int d), a, b, c, d);
PRELOADDEF(return, int, lockf, (int a, int b, off_t c), a, b, c);
PRELOADDEF(return, int, faccessat, (int a, const char *b, int c, int d), a, b, c, d);
PRELOADDEF(return, int, unlinkat, (int a, const char *b, int c), a, b, c);
PRELOADDEF(return, int, fchmodat, (int a, const char *b, mode_t c, int d), a, b, c, d);
PRELOADDEF(return, int, fchownat, (int a, const char *b, uid_t c, gid_t d, int e), a, b, c, d, e);

PRELOADDEF(return, size_t, fread, (void *a, size_t b, size_t c, FILE *d), a, b, c, d);
PRELOADDEF(return, size_t, fwrite, (const void *a, size_t b, size_t c, FILE *d), a, b, c, d);
PRELOADDEF(return, int, fputc, (int a, FILE *b), a, b);
PRELOADDEF(return, int, fputs, (const char *a, FILE *b), a, b);
PRELOADDEF(return, int, putchar, (int a), a);
PRELOADDEF(return, int, puts, (const char *a), a);
PRELOADDEF(return, int, vprintf, (const char *a, va_list b), a, b);
PRELOADDEF(return, int, vfprintf, (FILE *a, const char *b, va_list c), a, b, c);
PRELOADDEF(return, int, fflush, (FILE *a), a);

/* time family */

PRELOADDEF(return, time_t, time, (time_t *a), a);
PRELOADDEF(return, int, clock_gettime, (clockid_t a, struct timespec *b), a, b);
PRELOADDEF(return, int, gettimeofday, (struct timeval* a, struct timezone* b), a, b);
PRELOADDEF(return, struct tm *, localtime, (const time_t *a), a);
PRELOADDEF(return, struct tm *, localtime_r, (const time_t *a, struct tm *b), a, b);
PRELOADDEF(return, int, pthread_getcpuclockid, (pthread_t a, clockid_t *b), a, b);

/* name/address family */

/* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
PRELOADDEF(return, int, getnameinfo, (const struct sockaddr* a, socklen_t b, char * c, socklen_t d, char *e, socklen_t f, int g), a, b, c, d, e, f, g);
#else
PRELOADDEF(return, int, getnameinfo, (const struct sockaddr* a, socklen_t b, char * c, socklen_t d, char *e, socklen_t f, unsigned int g), a, b, c, d, e, f, g);
#endif
PRELOADDEF(return, int, gethostname, (char* a, size_t b), a, b);
PRELOADDEF(return, int, getaddrinfo, (const char *a, const char *b, const struct addrinfo *c, struct addrinfo **d), a, b, c, d);
PRELOADDEF(      , void, freeaddrinfo, (struct addrinfo *a), a);

PRELOADDEF(return, struct hostent*, gethostbyname, (const char* a), a);
PRELOADDEF(return, int, gethostbyname_r, (const char *a, struct hostent *b, char *c, size_t d, struct hostent **e, int *f), a, b, c, d, e, f);
PRELOADDEF(return, struct hostent*, gethostbyname2, (const char* a, int b), a, b);
PRELOADDEF(return, int, gethostbyname2_r, (const char *a, int b, struct hostent *c, char *d, size_t e, struct hostent **f, int *g), a, b, c, d, e, f, g);
PRELOADDEF(return, struct hostent*, gethostbyaddr, (const void* a, socklen_t b, int c), a, b, c);
PRELOADDEF(return, int, gethostbyaddr_r, (const void *a, socklen_t b, int c, struct hostent *d, char *e, size_t f, struct hostent **g, int *h), a, b, c, d, e, f, g, h);

/* random family */

PRELOADDEF(return, int, rand, ());
PRELOADDEF(return, int, rand_r, (unsigned int *a), a);
PRELOADDEF(      , void, srand, (unsigned int a), a);
PRELOADDEF(return, long int, random, ());
PRELOADDEF(return, int, random_r, (struct random_data *a, int32_t *b), a, b);
PRELOADDEF(      , void, srandom, (unsigned int a), a);
PRELOADDEF(return, int, srandom_r, (unsigned int a, struct random_data *b), a, b);

/* pid */

PRELOADDEF(return, pid_t, getpid, (void));
PRELOADDEF(return, pid_t, getppid, (void));

/* signals */

PRELOADDEF(return, int, sigaction, (int a, const struct sigaction* b, struct sigaction* c), a, b, c);

/* exit family */

PRELOADDEF(return, int, on_exit, (void (*a)(int , void *), void *b), a, b);
PRELOADDEF(return, int, atexit, (void (*a)(void)), a);
PRELOADDEF(return, int, __cxa_atexit, (void (*a) (void *), void *b, void *c), a, b, c);

/* pthread attributes */

PRELOADDEF(return, int, pthread_attr_init, (pthread_attr_t *a), a);
PRELOADDEF(return, int, pthread_getattr_np, (pthread_t a, pthread_attr_t *b), a, b);
PRELOADDEF(return, int, pthread_attr_destroy, (pthread_attr_t *a), a);
PRELOADDEF(return, int, pthread_attr_setinheritsched, (pthread_attr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_attr_getinheritsched, (const pthread_attr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_attr_setschedparam, (pthread_attr_t *a, const struct sched_param *b), a, b);
PRELOADDEF(return, int, pthread_attr_getschedparam, (const pthread_attr_t *a, struct sched_param *b), a, b);
PRELOADDEF(return, int, pthread_attr_setschedpolicy, (pthread_attr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_attr_getschedpolicy, (const pthread_attr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_attr_setscope, (pthread_attr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_attr_getscope, (const pthread_attr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_attr_setstacksize, (pthread_attr_t *a, size_t b), a, b);
PRELOADDEF(return, int, pthread_attr_getstacksize, (const pthread_attr_t *a, size_t *b), a, b);
PRELOADDEF(return, int, pthread_attr_setstackaddr, (pthread_attr_t *a, void *b), a, b);
PRELOADDEF(return, int, pthread_attr_getstackaddr, (const pthread_attr_t *a, void **b), a, b);
PRELOADDEF(return, int, pthread_attr_setdetachstate, (pthread_attr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_attr_getdetachstate, (const pthread_attr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_attr_setguardsize, (pthread_attr_t *a, size_t b), a, b);
PRELOADDEF(return, int, pthread_attr_getguardsize, (const pthread_attr_t *a, size_t *b), a, b);
PRELOADDEF(return, int, pthread_attr_setname_np, (pthread_attr_t *a, char *b), a, b);
PRELOADDEF(return, int, pthread_attr_getname_np, (const pthread_attr_t *a, char **b), a, b);
PRELOADDEF(return, int, pthread_attr_setprio_np, (pthread_attr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_attr_getprio_np, (const pthread_attr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_attr_getstack, (const pthread_attr_t *a, void **b, size_t *c), a, b, c);
PRELOADDEF(return, int, pthread_attr_setstack, (pthread_attr_t *a, void *b, size_t c), a, b, c);
PRELOADDEF(return, int, pthread_attr_setaffinity_np, (pthread_attr_t *a, size_t b, const cpu_set_t *c), a, b, c);
PRELOADDEF(return, int, pthread_attr_getaffinity_np, (const pthread_attr_t *a, size_t b, cpu_set_t *c), a, b, c);
PRELOADDEF(return, int, pthread_getattr_default_np, (pthread_attr_t *a), a);
PRELOADDEF(return, int, pthread_setattr_default_np, (const pthread_attr_t *a), a);

/* pthread threads */

PRELOADDEF(return, int, pthread_create, (pthread_t *a, const pthread_attr_t *b, void *(*c)(void *), void *d), a, b, c, d);
PRELOADDEF(return, int, pthread_detach, (pthread_t a), a);
PRELOADDEF(return, int, __pthread_detach, (pthread_t a), a);
PRELOADDEF(return, pthread_t, pthread_self, (void));
PRELOADDEF(return, int, pthread_equal, (pthread_t a, pthread_t b), a, b);
PRELOADDEF(return, int, pthread_yield, (void));
PRELOADDEF(return, int, pthread_yield_np, (void));
PRELOADDEF(return, int, pthread_join, (pthread_t a, void **b), a, b);
PRELOADDEF(return, int, pthread_once, (pthread_once_t *a, void (*b)(void)), a, b);
PRELOADDEF(return, int, pthread_sigmask, (int a, const sigset_t *b, sigset_t *c), a, b, c);
PRELOADDEF(return, int, pthread_kill, (pthread_t a, int b), a, b);
PRELOADDEF(return, int, pthread_abort, (pthread_t a), a);
PRELOADDEF(return, int, pthread_tryjoin_np, (pthread_t a, void **b), a, b);
PRELOADDEF(return, int, pthread_timedjoin_np, (pthread_t a, void **b, const struct timespec *c), a, b, c);
PRELOADDEF(return, int, pthread_getname_np, (pthread_t a, char *b, size_t c), a, b, c);
PRELOADDEF(return, int, pthread_setname_np, (pthread_t a, const char *b), a, b);
PRELOADDEF(return, int, pthread_setaffinity_np, (pthread_t a, size_t b, const cpu_set_t *c), a, b, c);
PRELOADDEF(return, int, pthread_getaffinity_np, (pthread_t a, size_t b, cpu_set_t *c), a, b, c);

/* concurrency */

PRELOADDEF(return, int, pthread_getconcurrency, (void));
PRELOADDEF(return, int, pthread_setconcurrency, (int a), a);

/* pthread context */

PRELOADDEF(return, int, pthread_key_create, (pthread_key_t *a, void (*b)(void *)), a, b);
PRELOADDEF(return, int, pthread_key_delete, (pthread_key_t a), a);
PRELOADDEF(return, int, pthread_setspecific, (pthread_key_t a, const void *b), a, b);
PRELOADDEF(return, void*, pthread_getspecific, (pthread_key_t a), a);

/* pthread cancel */

PRELOADDEF(return, int, pthread_cancel, (pthread_t a), a);
PRELOADDEF(      , void, pthread_testcancel, (void));
PRELOADDEF(return, int, pthread_setcancelstate, (int a, int *b), a, b);
PRELOADDEF(return, int, pthread_setcanceltype, (int a, int *b), a, b);

/* pthread scheduler */

PRELOADDEF(return, int, pthread_setschedparam, (pthread_t a, int b, const struct sched_param *c), a, b, c);
PRELOADDEF(return, int, pthread_getschedparam, (pthread_t a, int *b, struct sched_param *c), a, b, c);
PRELOADDEF(return, int, pthread_setschedprio, (pthread_t a, int b), a, b);

/* pthread cleanup */
/* the commented out PRELOADDEFs here are macros, so cannot be interposed */

//PRELOADDEF(      , void, pthread_cleanup_push, (void (*a)(void *), void *b), a, b);
PRELOADDEF(      , void, __pthread_register_cancel, (__pthread_unwind_buf_t *a), a);
//PRELOADDEF(      , void, pthread_cleanup_pop, (int a), a, b);
PRELOADDEF(      , void, __pthread_unregister_cancel, (__pthread_unwind_buf_t *a), a);
//PRELOADDEF(      , void, pthread_cleanup_push_defer_np, (void (*a)(void *), void *b), a, b);
PRELOADDEF(      , void, __pthread_register_cancel_defer, (__pthread_unwind_buf_t *a), a);
//PRELOADDEF(      , void, pthread_cleanup_pop_restore_np, (int a), a, b);
PRELOADDEF(      , void, __pthread_unregister_cancel_restore, (__pthread_unwind_buf_t *a), a);

/* forking */

PRELOADDEF(return, int, pthread_atfork, (void (*a)(void), void (*b)(void), void (*c)(void)), a, b, c);

/* pthread mutex attributes */

PRELOADDEF(return, int, pthread_mutexattr_init, (pthread_mutexattr_t *a), a);
PRELOADDEF(return, int, pthread_mutexattr_destroy, (pthread_mutexattr_t *a), a);
PRELOADDEF(return, int, pthread_mutexattr_setprioceiling, (pthread_mutexattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_getprioceiling, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_setprotocol, (pthread_mutexattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_getprotocol, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_setpshared, (pthread_mutexattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_getpshared, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_settype, (pthread_mutexattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_gettype, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_getrobust, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_getrobust_np, (const pthread_mutexattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_setrobust, (pthread_mutexattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_mutexattr_setrobust_np, (pthread_mutexattr_t *a, int b), a, b);

/* pthread mutex */

PRELOADDEF(return, int, pthread_mutex_init, (pthread_mutex_t *a, const pthread_mutexattr_t *b), a, b);
PRELOADDEF(return, int, pthread_mutex_destroy, (pthread_mutex_t *a), a);
PRELOADDEF(return, int, pthread_mutex_setprioceiling, (pthread_mutex_t *a, int b, int *c), a, b, c);
PRELOADDEF(return, int, pthread_mutex_getprioceiling, (const pthread_mutex_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_mutex_lock, (pthread_mutex_t *a), a);
PRELOADDEF(return, int, pthread_mutex_trylock, (pthread_mutex_t *a), a);
PRELOADDEF(return, int, pthread_mutex_unlock, (pthread_mutex_t *a), a);
PRELOADDEF(return, int, pthread_mutex_timedlock, (pthread_mutex_t *a, const struct timespec *b), a, b);
PRELOADDEF(return, int, pthread_mutex_consistent, (pthread_mutex_t *a), a);
PRELOADDEF(return, int, pthread_mutex_consistent_np, (pthread_mutex_t *a), a);

/* pthread read-write lock attributes */

PRELOADDEF(return, int, pthread_rwlockattr_init, (pthread_rwlockattr_t *a), a);
PRELOADDEF(return, int, pthread_rwlockattr_destroy, (pthread_rwlockattr_t *a), a);
PRELOADDEF(return, int, pthread_rwlockattr_setpshared, (pthread_rwlockattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_rwlockattr_getpshared, (const pthread_rwlockattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_rwlockattr_getkind_np, (const pthread_rwlockattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_rwlockattr_setkind_np, (pthread_rwlockattr_t *a, int b), a, b);

/* pthread read-write locks */

PRELOADDEF(return, int, pthread_rwlock_init, (pthread_rwlock_t *a, const pthread_rwlockattr_t *b), a, b);
PRELOADDEF(return, int, pthread_rwlock_destroy, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_rdlock, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_tryrdlock, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_wrlock, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_trywrlock, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_unlock, (pthread_rwlock_t *a), a);
PRELOADDEF(return, int, pthread_rwlock_timedrdlock, (pthread_rwlock_t *a, const struct timespec *b), a, b);
PRELOADDEF(return, int, pthread_rwlock_timedwrlock, (pthread_rwlock_t *a, const struct timespec *b), a, b);

/* pthread spinlocks */

PRELOADDEF(return, int, pthread_spin_init, (pthread_spinlock_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_spin_destroy, (pthread_spinlock_t *a), a);
PRELOADDEF(return, int, pthread_spin_lock, (pthread_spinlock_t *a), a);
PRELOADDEF(return, int, pthread_spin_trylock, (pthread_spinlock_t *a), a);
PRELOADDEF(return, int, pthread_spin_unlock, (pthread_spinlock_t *a), a);

/* pthread barrier attributes */

PRELOADDEF(return, int, pthread_barrierattr_init, (pthread_barrierattr_t *a), a);
PRELOADDEF(return, int, pthread_barrierattr_destroy, (pthread_barrierattr_t *a), a);
PRELOADDEF(return, int, pthread_barrierattr_getpshared, (const pthread_barrierattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_barrierattr_setpshared, (pthread_barrierattr_t *a, int b), a, b);

/* pthread barriers */

PRELOADDEF(return, int, pthread_barrier_init, (pthread_barrier_t *a, const pthread_barrierattr_t *b, unsigned int c), a, b, c);
PRELOADDEF(return, int, pthread_barrier_destroy, (pthread_barrier_t *a), a);
PRELOADDEF(return, int, pthread_barrier_wait, (pthread_barrier_t *a), a);

/* pthread condition attributes */

PRELOADDEF(return, int, pthread_condattr_init, (pthread_condattr_t *a), a);
PRELOADDEF(return, int, pthread_condattr_destroy, (pthread_condattr_t *a), a);
PRELOADDEF(return, int, pthread_condattr_setpshared, (pthread_condattr_t *a, int b), a, b);
PRELOADDEF(return, int, pthread_condattr_getpshared, (const pthread_condattr_t *a, int *b), a, b);
PRELOADDEF(return, int, pthread_condattr_setclock, (pthread_condattr_t *a, clockid_t b), a, b);
PRELOADDEF(return, int, pthread_condattr_getclock, (const pthread_condattr_t *a, clockid_t *b), a, b);

/* pthread conditions */

PRELOADDEF(return, int, pthread_cond_init, (pthread_cond_t *a, const pthread_condattr_t *b), a, b);
PRELOADDEF(return, int, pthread_cond_destroy, (pthread_cond_t *a), a);
PRELOADDEF(return, int, pthread_cond_broadcast, (pthread_cond_t *a), a);
PRELOADDEF(return, int, pthread_cond_signal, (pthread_cond_t *a), a);
PRELOADDEF(return, int, pthread_cond_wait, (pthread_cond_t *a, pthread_mutex_t *b), a, b);
PRELOADDEF(return, int, pthread_cond_timedwait, (pthread_cond_t *a, pthread_mutex_t *b, const struct timespec *c), a, b, c);

