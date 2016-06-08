/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROCESS_H_
#define SHD_PROCESS_H_

#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/statvfs.h>
#include <poll.h>

#include "shadow.h"

#if !defined __USE_LARGEFILE64
typedef off_t off64_t;
#endif

Process* process_new(gpointer host, GQuark programID, guint processID,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void process_ref(Process* proc);
void process_unref(Process* proc);

void process_start(Process* proc);
void process_continue(Process* proc);
void process_stop(Process* proc);

gboolean process_wantsNotify(Process* proc, gint epollfd);
gboolean process_isRunning(Process* proc);
gboolean process_shouldEmulate(Process* proc);

gboolean process_addAtExitCallback(Process* proc, gpointer userCallback, gpointer userArgument,
        gboolean shouldPassArgument);

/*****************************************************************
 * Begin virtual process emulation of pthread and syscalls.
 * These functions have been interposed by the preload library
 * to hijack control over the flow of execution.
 *****************************************************************/

/* memory allocation family */

void* process_emu_malloc(Process* proc, size_t size);
void* process_emu_calloc(Process* proc, size_t nmemb, size_t size);
void* process_emu_realloc(Process* proc, void *ptr, size_t size);
void process_emu_free(Process* proc, void *ptr);
int process_emu_posix_memalign(Process* proc, void** memptr, size_t alignment, size_t size);
void* process_emu_memalign(Process* proc, size_t blocksize, size_t bytes);
void* process_emu_aligned_alloc(Process* proc, size_t alignment, size_t size);
void* process_emu_valloc(Process* proc, size_t size);
void* process_emu_pvalloc(Process* proc, size_t size);
void* process_emu_mmap(Process* proc, void *addr, size_t length, int prot, int flags, int fd, off_t offset);

/* event family */

int process_emu_epoll_create(Process* proc, int size);
int process_emu_epoll_create1(Process* proc, int flags);
int process_emu_epoll_ctl(Process* proc, int epfd, int op, int fd, struct epoll_event *event);
int process_emu_epoll_wait(Process* proc, int epfd, struct epoll_event *events, int maxevents, int timeout);
int process_emu_epoll_pwait(Process* proc, int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss);

/* socket/io family */

int process_emu_socket(Process* proc, int domain, int type, int protocol);
int process_emu_socketpair(Process* proc, int domain, int type, int protocol, int fds[2]);
int process_emu_bind(Process* proc, int fd, const struct sockaddr* addr, socklen_t len);
int process_emu_getsockname(Process* proc, int fd, struct sockaddr* addr, socklen_t* len);
int process_emu_connect(Process* proc, int fd, const struct sockaddr* addr, socklen_t len);
int process_emu_getpeername(Process* proc, int fd, struct sockaddr* addr, socklen_t* len);
ssize_t process_emu_send(Process* proc, int fd, const void *buf, size_t n, int flags);
ssize_t process_emu_sendto(Process* proc, int fd, const void *buf, size_t n, int flags, const struct sockaddr* addr, socklen_t addr_len);
ssize_t process_emu_sendmsg(Process* proc, int fd, const struct msghdr *message, int flags);
ssize_t process_emu_recv(Process* proc, int fd, void *buf, size_t n, int flags);
ssize_t process_emu_recvfrom(Process* proc, int fd, void *buf, size_t n, int flags, struct sockaddr* addr, socklen_t *restrict addr_len);
ssize_t process_emu_recvmsg(Process* proc, int fd, struct msghdr *message, int flags);
int process_emu_getsockopt(Process* proc, int fd, int level, int optname, void* optval, socklen_t* optlen);
int process_emu_setsockopt(Process* proc, int fd, int level, int optname, const void *optval, socklen_t optlen);
int process_emu_listen(Process* proc, int fd, int n);
int process_emu_accept(Process* proc, int fd, struct sockaddr* addr, socklen_t* addr_len);
int process_emu_accept4(Process* proc, int fd, struct sockaddr* addr, socklen_t* addr_len, int flags);
int process_emu_shutdown(Process* proc, int fd, int how);
ssize_t process_emu_read(Process* proc, int fd, void *buff, size_t numbytes);
ssize_t process_emu_write(Process* proc, int fd, const void *buff, size_t n);
ssize_t process_emu_readv(Process* proc, int fd, const struct iovec *iov, int iovcnt);
ssize_t process_emu_writev(Process* proc, int fd, const struct iovec *iov, int iovcnt);
ssize_t process_emu_pread(Process* proc, int fd, void *buff, size_t numbytes, off_t offset);
ssize_t process_emu_pwrite(Process* proc, int fd, const void *buf, size_t nbytes, off_t offset);
int process_emu_close(Process* proc, int fd);
int process_emu_fcntl(Process* proc, int fd, int cmd, void* argp);
int process_emu_ioctl(Process* proc, int fd, unsigned long int request, void* argp);
int process_emu_pipe2(Process* proc, int pipefds[2], int flags);
int process_emu_pipe(Process* proc, int pipefds[2]);
int process_emu_getifaddrs(Process* proc, struct ifaddrs **ifap);
void process_emu_freeifaddrs(Process* proc, struct ifaddrs *ifa);
int process_emu_eventfd(Process* proc, int initval, int flags);
int process_emu_timerfd_create(Process* proc, int clockid, int flags);
int process_emu_timerfd_settime(Process* proc, int fd, int flags, const struct itimerspec *new_value, struct itimerspec *old_value);
int process_emu_timerfd_gettime(Process* proc, int fd, struct itimerspec *curr_value);

/* file specific */

int process_emu_fileno(Process* proc, FILE *stream);
int process_emu_open(Process* proc, const char *pathname, int flags, mode_t mode);
int process_emu_open64(Process* proc, const char *pathname, int flags, mode_t mode);
int process_emu_creat(Process* proc, const char *pathname, mode_t mode);
FILE *process_emu_fopen(Process* proc, const char *path, const char *mode);
FILE *process_emu_fopen64(Process* proc, const char *path, const char *mode);
FILE *process_emu_fdopen(Process* proc, int fd, const char *mode);
int process_emu_dup(Process* proc, int oldfd);
int process_emu_dup2(Process* proc, int oldfd, int newfd);
int process_emu_dup3(Process* proc, int oldfd, int newfd, int flags);
int process_emu_fclose(Process* proc, FILE *fp);;
int process_emu___fxstat (Process* proc, int ver, int fd, struct stat *buf);
int process_emu___fxstat64 (Process* proc, int ver, int fd, struct stat64 *buf);
int process_emu_fstatfs (Process* proc, int fd, struct statfs *buf);
int process_emu_fstatfs64 (Process* proc, int fd, struct statfs64 *buf);
off_t process_emu_lseek(Process* proc, int fd, off_t offset, int whence);
off64_t process_emu_lseek64(Process* proc, int fd, off64_t offset, int whence);
int process_emu_flock(Process* proc, int fd, int operation);
int process_emu_fsync(Process* proc, int fd);
int process_emu_ftruncate(Process* proc, int fd, off_t length);
int process_emu_ftruncate64(Process* proc, int fd, off64_t length);
int process_emu_posix_fallocate(Process* proc, int fd, off_t offset, off_t len);

int process_emu_fstatvfs(Process* proc, int fd, struct statvfs *buf);
int process_emu_fdatasync(Process* proc, int fd);
int process_emu_syncfs(Process* proc, int fd);
int process_emu_fallocate(Process* proc, int fd, int mode, off_t offset, off_t len);
int process_emu_fexecve(Process* proc, int fd, char *const argv[], char *const envp[]);
long process_emu_fpathconf(Process* proc, int fd, int name);
int process_emu_fchdir(Process* proc, int fd);
int process_emu_fchown(Process* proc, int fd, uid_t owner, gid_t group);
int process_emu_fchmod(Process* proc, int fd, mode_t mode);
int process_emu_posix_fadvise(Process* proc, int fd, off_t offset, off_t len, int advice);
int process_emu_lockf(Process* proc, int fd, int cmd, off_t len);
int process_emu_openat(Process* proc, int dirfd, const char *pathname, int flags, mode_t mode);
int process_emu_faccessat(Process* proc, int dirfd, const char *pathname, int mode, int flags);
int process_emu_unlinkat(Process* proc, int dirfd, const char *pathname, int flags);
int process_emu_fchmodat(Process* proc, int dirfd, const char *pathname, mode_t mode, int flags);
int process_emu_fchownat(Process* proc, int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);

size_t process_emu_fread(Process* proc, void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t process_emu_fwrite(Process* proc, const void *ptr, size_t size, size_t nmemb, FILE *stream);
int process_emu_fputc(Process* proc, int c, FILE *stream);
int process_emu_fputs(Process* proc, const char *s, FILE *stream);
int process_emu_putchar(Process* proc, int c);
int process_emu_puts(Process* proc, const char *s);
int process_emu_vprintf(Process* proc, const char *format, va_list ap);
int process_emu_vfprintf(Process* proc, FILE *stream, const char *format, va_list ap);
int process_emu_fflush(Process* proc, FILE *stream);

/* time family */

time_t process_emu_time(Process* proc, time_t *t);
int process_emu_clock_gettime(Process* proc, clockid_t clk_id, struct timespec *tp);
int process_emu_gettimeofday(Process* proc, struct timeval* tv, struct timezone *tz);
struct tm* process_emu_localtime(Process* proc, const time_t *timep);
struct tm* process_emu_localtime_r(Process* proc, const time_t *timep, struct tm *result);

/* name/address family */

int process_emu_gethostname(Process* proc, char* name, size_t len);
int process_emu_getaddrinfo(Process* proc, const char *node, const char *service,
        const struct addrinfo *hints, struct addrinfo **res);
void process_emu_freeaddrinfo(Process* proc, struct addrinfo *res);
int process_emu_getnameinfo(Process* proc, const struct sockaddr* sa, socklen_t salen,
        char * host, socklen_t hostlen, char *serv, socklen_t servlen,
        /* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
        int flags);
#else
        unsigned int flags);
#endif
struct hostent* process_emu_gethostbyname(Process* proc, const gchar* name);
int process_emu_gethostbyname_r(Process* proc, const gchar *name, struct hostent *ret, gchar *buf,
        gsize buflen, struct hostent **result, gint *h_errnop);
struct hostent* process_emu_gethostbyname2(Process* proc, const gchar* name, gint af);
int process_emu_gethostbyname2_r(Process* proc, const gchar *name, gint af, struct hostent *ret,
        gchar *buf, gsize buflen, struct hostent **result, gint *h_errnop);
struct hostent* process_emu_gethostbyaddr(Process* proc, const void* addr, socklen_t len, gint type);
int process_emu_gethostbyaddr_r(Process* proc, const void *addr, socklen_t len, gint type,
        struct hostent *ret, char *buf, gsize buflen, struct hostent **result, gint *h_errnop);

/* random family */

int process_emu_rand(Process* proc);
int process_emu_rand_r(Process* proc, unsigned int *seedp);
void process_emu_srand(Process* proc, unsigned int seed);
long int process_emu_random(Process* proc);
int process_emu_random_r(Process* proc, struct random_data *buf, int32_t *result);
void process_emu_srandom(Process* proc, unsigned int seed);
int process_emu_srandom_r(Process* proc, unsigned int seed, struct random_data *buf);

/* exit family */

void process_emu_exit(Process* proc, int status);
int process_emu_on_exit(Process* proc, void (*function)(int , void *), void *arg);
int process_emu_atexit(Process* proc, void (*func)(void));
int process_emu___cxa_atexit(Process* proc, void (*func) (void *), void * arg, void * dso_handle);

/* pthread attributes */

int process_emu_pthread_attr_init(Process* proc, pthread_attr_t *attr);
int process_emu_pthread_attr_destroy(Process* proc, pthread_attr_t *attr);
int process_emu_pthread_attr_setinheritsched(Process* proc, pthread_attr_t *attr, int inheritsched);
int process_emu_pthread_attr_getinheritsched(Process* proc, const pthread_attr_t *attr, int *inheritsched);
int process_emu_pthread_attr_setschedparam(Process* proc, pthread_attr_t *attr, const struct sched_param *schedparam);
int process_emu_pthread_attr_getschedparam(Process* proc, const pthread_attr_t *attr, struct sched_param *schedparam);
int process_emu_pthread_attr_setschedpolicy(Process* proc, pthread_attr_t *attr, int schedpolicy);
int process_emu_pthread_attr_getschedpolicy(Process* proc, const pthread_attr_t *attr, int *schedpolicy);
int process_emu_pthread_attr_setscope(Process* proc, pthread_attr_t *attr, int scope);
int process_emu_pthread_attr_getscope(Process* proc, const pthread_attr_t *attr, int *scope);
int process_emu_pthread_attr_setstacksize(Process* proc, pthread_attr_t *attr, size_t stacksize);
int process_emu_pthread_attr_getstacksize(Process* proc, const pthread_attr_t *attr, size_t *stacksize);
int process_emu_pthread_attr_setstackaddr(Process* proc, pthread_attr_t *attr, void *stackaddr);
int process_emu_pthread_attr_getstackaddr(Process* proc, const pthread_attr_t *attr, void **stackaddr);
int process_emu_pthread_attr_setdetachstate(Process* proc, pthread_attr_t *attr, int detachstate);
int process_emu_pthread_attr_getdetachstate(Process* proc, const pthread_attr_t *attr, int *detachstate);
int process_emu_pthread_attr_setguardsize(Process* proc, pthread_attr_t *attr, size_t stacksize);
int process_emu_pthread_attr_getguardsize(Process* proc, const pthread_attr_t *attr, size_t *stacksize);
int process_emu_pthread_attr_setname_np(Process* proc, pthread_attr_t *attr, char *name);
int process_emu_pthread_attr_getname_np(Process* proc, const pthread_attr_t *attr, char **name);
int process_emu_pthread_attr_setprio_np(Process* proc, pthread_attr_t *attr, int prio);
int process_emu_pthread_attr_getprio_np(Process* proc, const pthread_attr_t *attr, int *prio);

/* pthread threads */

int process_emu_pthread_create(Process* proc, pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
int process_emu_pthread_detach(Process* proc, pthread_t thread);
int process_emu___pthread_detach(Process* proc, pthread_t thread);
pthread_t process_emu_pthread_self(Process* proc);
int process_emu_pthread_equal(Process* proc, pthread_t t1, pthread_t t2);
int process_emu_pthread_yield(Process* proc);
int process_emu_pthread_yield_np(Process* proc);
void process_emu_pthread_exit(Process* proc, void *value_ptr);
int process_emu_pthread_join(Process* proc, pthread_t thread, void **value_ptr);
int process_emu_pthread_once(Process* proc, pthread_once_t *once_control, void (*init_routine)(void));
int process_emu_pthread_sigmask(Process* proc, int how, const sigset_t *set, sigset_t *oset);
int process_emu_pthread_kill(Process* proc, pthread_t thread, int sig);
int process_emu_pthread_abort(Process* proc, pthread_t thread);

/* concurrency */

int process_emu_pthread_getconcurrency(Process* proc);
int process_emu_pthread_setconcurrency(Process* proc, int new_level);

/* pthread context */

int process_emu_pthread_key_create(Process* proc, pthread_key_t *key, void (*destructor)(void *));
int process_emu_pthread_key_delete(Process* proc, pthread_key_t key);
int process_emu_pthread_setspecific(Process* proc, pthread_key_t key, const void *value);
void *process_emu_pthread_getspecific(Process* proc, pthread_key_t key);

/* pthread cancel */

int process_emu_pthread_cancel(Process* proc, pthread_t thread);
void process_emu_pthread_testcancel(Process* proc);
int process_emu_pthread_setcancelstate(Process* proc, int state, int *oldstate);
int process_emu_pthread_setcanceltype(Process* proc, int type, int *oldtype);

/* pthread scheduler */

int process_emu_pthread_setschedparam(Process* proc, pthread_t pthread, int policy, const struct sched_param *param);
int process_emu_pthread_getschedparam(Process* proc, pthread_t pthread, int *policy, struct sched_param *param);

/* pthread cleanup */

void process_emu_pthread_cleanup_push(Process* proc, void (*routine)(void *), void *arg);
void process_emu_pthread_cleanup_pop(Process* proc, int execute);

/* forking */

int process_emu_pthread_atfork(Process* proc, void (*prepare)(void), void (*parent)(void), void (*child)(void));

/* pthread mutex attributes */

int process_emu_pthread_mutexattr_init(Process* proc, pthread_mutexattr_t *attr);
int process_emu_pthread_mutexattr_destroy(Process* proc, pthread_mutexattr_t *attr);
int process_emu_pthread_mutexattr_setprioceiling(Process* proc, pthread_mutexattr_t *attr, int prioceiling);
int process_emu_pthread_mutexattr_getprioceiling(Process* proc, const pthread_mutexattr_t *attr, int *prioceiling);
int process_emu_pthread_mutexattr_setprotocol(Process* proc, pthread_mutexattr_t *attr, int protocol);
int process_emu_pthread_mutexattr_getprotocol(Process* proc, const pthread_mutexattr_t *attr, int *protocol);
int process_emu_pthread_mutexattr_setpshared(Process* proc, pthread_mutexattr_t *attr, int pshared);
int process_emu_pthread_mutexattr_getpshared(Process* proc, const pthread_mutexattr_t *attr, int *pshared);
int process_emu_pthread_mutexattr_settype(Process* proc, pthread_mutexattr_t *attr, int type);
int process_emu_pthread_mutexattr_gettype(Process* proc, const pthread_mutexattr_t *attr, int *type);

/* pthread mutex */

int process_emu_pthread_mutex_init(Process* proc, pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int process_emu_pthread_mutex_destroy(Process* proc, pthread_mutex_t *mutex);
int process_emu_pthread_mutex_setprioceiling(Process* proc, pthread_mutex_t *mutex, int prioceiling, int *old_ceiling);
int process_emu_pthread_mutex_getprioceiling(Process* proc, const pthread_mutex_t *mutex, int *prioceiling);
int process_emu_pthread_mutex_lock(Process* proc, pthread_mutex_t *mutex);
int process_emu_pthread_mutex_trylock(Process* proc, pthread_mutex_t *mutex);
int process_emu_pthread_mutex_unlock(Process* proc, pthread_mutex_t *mutex);

/* pthread lock attributes */

int process_emu_pthread_rwlockattr_init(Process* proc, pthread_rwlockattr_t *attr);
int process_emu_pthread_rwlockattr_destroy(Process* proc, pthread_rwlockattr_t *attr);
int process_emu_pthread_rwlockattr_setpshared(Process* proc, pthread_rwlockattr_t *attr, int pshared);
int process_emu_pthread_rwlockattr_getpshared(Process* proc, const pthread_rwlockattr_t *attr, int *pshared);

/* pthread locks */

int process_emu_pthread_rwlock_init(Process* proc, pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr);
int process_emu_pthread_rwlock_destroy(Process* proc, pthread_rwlock_t *rwlock);
int process_emu_pthread_rwlock_rdlock(Process* proc, pthread_rwlock_t *rwlock);
int process_emu_pthread_rwlock_tryrdlock(Process* proc, pthread_rwlock_t *rwlock);
int process_emu_pthread_rwlock_wrlock(Process* proc, pthread_rwlock_t *rwlock);
int process_emu_pthread_rwlock_trywrlock(Process* proc, pthread_rwlock_t *rwlock);
int process_emu_pthread_rwlock_unlock(Process* proc, pthread_rwlock_t *rwlock);

/* pthread condition attributes */

int process_emu_pthread_condattr_init(Process* proc, pthread_condattr_t *attr);
int process_emu_pthread_condattr_destroy(Process* proc, pthread_condattr_t *attr);
int process_emu_pthread_condattr_setpshared(Process* proc, pthread_condattr_t *attr, int pshared);
int process_emu_pthread_condattr_getpshared(Process* proc, const pthread_condattr_t *attr, int *pshared);
int process_emu_pthread_condattr_setclock(Process* proc, pthread_condattr_t *attr, clockid_t clock_id);
int process_emu_pthread_condattr_getclock(Process* proc, const pthread_condattr_t *attr, clockid_t* clock_id);

/* pthread conditions */

int process_emu_pthread_cond_init(Process* proc, pthread_cond_t *cond, const pthread_condattr_t *attr);
int process_emu_pthread_cond_destroy(Process* proc, pthread_cond_t *cond);
int process_emu_pthread_cond_broadcast(Process* proc, pthread_cond_t *cond);
int process_emu_pthread_cond_signal(Process* proc, pthread_cond_t *cond);
int process_emu_pthread_cond_wait(Process* proc, pthread_cond_t *cond, pthread_mutex_t *mutex);
int process_emu_pthread_cond_timedwait(Process* proc, pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime);


pid_t process_emu_fork(Process* proc);
unsigned int process_emu_sleep(Process* proc, unsigned int sec);
int process_emu_system(Process* proc, const char *cmd);
int process_emu_nanosleep(Process* proc, const struct timespec *rqtp, struct timespec *rmtp);
int process_emu_usleep(Process* proc, unsigned int sec);
int process_emu_sigwait(Process* proc, const sigset_t *set, int *sig);
pid_t process_emu_waitpid(Process* proc, pid_t pid, int *status, int options);
int process_emu_select(Process* proc, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int process_emu_pselect(Process* proc, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask);
int process_emu_poll(Process* proc, struct pollfd *pfd, nfds_t nfd, int timeout);
int process_emu_ppoll(Process* proc, struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask);

#endif /* SHD_PROCESS_H_ */
