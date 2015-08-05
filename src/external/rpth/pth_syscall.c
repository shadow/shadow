/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  pth_syscall.c: Pth direct syscall support
*/
                             /* ``Free Software: generous programmers
                                  from around the world all join
                                  forces to help you shoot yourself
                                  in the foot for free.''
                                                 -- Unknown         */
/*
 * Prevent system includes from declaring the syscalls in order to avoid
 * prototype mismatches. In theory those mismatches should not happen
 * at all, but slight (but still compatible) differences (ssize_t vs.
 * size_t, etc) can lead to a compile-time failure (although run-time
 * would be ok). Hence protect ourself from this situation.
 */
#define fork          __pth_sys_fork
#define waitpid       __pth_sys_waitpid
#define system        __pth_sys_system
#define nanosleep     __pth_sys_nanosleep
#define usleep        __pth_sys_usleep
#define sleep         __pth_sys_sleep
#define sigprocmask   __pth_sys_sigmask
#define sigwait       __pth_sys_sigwait
#define select        __pth_sys_select
#define pselect       __pth_sys_pselect
#define poll          __pth_sys_poll
#define connect       __pth_sys_connect
#define accept        __pth_sys_accept
#define read          __pth_sys_read
#define write         __pth_sys_write
#define readv         __pth_sys_readv
#define writev        __pth_sys_writev
#define recv          __pth_sys_recv
#define send          __pth_sys_send
#define recvfrom      __pth_sys_recvfrom
#define sendto        __pth_sys_sendto
#define pread         __pth_sys_pread
#define pwrite        __pth_sys_pwrite

/* include the private header and this way system headers */
#include "pth_p.h"

/* some exported variables for object layer checks */
int pth_syscall_soft = PTH_SYSCALL_SOFT;
int pth_syscall_hard = PTH_SYSCALL_HARD;

#if cpp
#if PTH_SYSCALL_HARD
/* hard syscall mapping */
#if HAVE_SYS_SYSCALL_H
#include <sys/syscall.h>
#endif
#ifdef HAVE_SYS_SOCKETCALL_H
#include <sys/socketcall.h>
#endif
#define pth_sc(func) pth_sc_##func
#else /* !PTH_SYSCALL_HARD */
/* no hard syscall mapping */
#define pth_sc(func) func
#endif /* PTH_SYSCALL_HARD */
#endif /* cpp */

/*
 * Unprotect us from the namespace conflict with the
 * syscall prototypes in system headers.
 */
#undef fork
#undef waitpid
#undef system
#undef nanosleep
#undef usleep
#undef sleep
#undef sigprocmask
#undef sigwait
#undef select
#undef pselect
#undef poll
#undef connect
#undef accept
#undef read
#undef write
#undef readv
#undef writev
#undef recv
#undef send
#undef recvfrom
#undef sendto
#undef pread
#undef pwrite

/* internal data structures */
#if cpp
typedef int (*pth_syscall_fct_t)();
typedef struct {
    char             *name;    /* name of system/function call */
    pth_syscall_fct_t addr;    /* address of wrapped system/function call */
} pth_syscall_fct_tab_t;
typedef struct {
    char             *path;    /* path to dynamic library */
    void             *handle;  /* handle of dynamic library */
} pth_syscall_lib_tab_t;
#endif

#if PTH_SYSCALL_HARD

/* NUL-spiked copy of library paths */
static char *pth_syscall_libs = NULL;

/* table of dynamic libraries and their resolving handles */
static pth_syscall_lib_tab_t pth_syscall_lib_tab[128] = {
    { NULL, NULL }
};

/* table of syscalls and their resolved function pointers */
intern pth_syscall_fct_tab_t pth_syscall_fct_tab[] = {
    /* Notice: order must match the macro values above */
#define PTH_SCF_fork          0
#define PTH_SCF_waitpid       1
#define PTH_SCF_system        2
#define PTH_SCF_nanosleep     3
#define PTH_SCF_usleep        4
#define PTH_SCF_sleep         5
#define PTH_SCF_sigprocmask   6
#define PTH_SCF_sigwait       7
#define PTH_SCF_select        8
#define PTH_SCF_poll          9
#define PTH_SCF_connect       10
#define PTH_SCF_accept        11
#define PTH_SCF_read          12
#define PTH_SCF_write         13
#define PTH_SCF_readv         14
#define PTH_SCF_writev        15
#define PTH_SCF_recv          16
#define PTH_SCF_send          17
#define PTH_SCF_recvfrom      18
#define PTH_SCF_sendto        19
#define PTH_SCF_pread         20
#define PTH_SCF_pwrite        21
    { "fork",        NULL },
    { "waitpid",     NULL },
    { "system",      NULL },
    { "nanosleep",   NULL },
    { "usleep",      NULL },
    { "sleep",       NULL },
    { "sigprocmask", NULL },
    { "sigwait",     NULL },
    { "select",      NULL },
    { "poll",        NULL },
    { "connect",     NULL },
    { "accept",      NULL },
    { "read",        NULL },
    { "write",       NULL },
    { "readv",       NULL },
    { "writev",      NULL },
    { "recv",        NULL },
    { "send",        NULL },
    { "recvfrom",    NULL },
    { "sendto",      NULL },
    { "pread",       NULL },
    { "pwrite",      NULL },
    { NULL,          NULL }
};
#endif

/* syscall wrapping initialization */
intern void pth_syscall_init(void)
{
#if PTH_SYSCALL_HARD
    int i;
    int j;
    char *cpLib;
    char *cp;

    /* fill paths of libraries into internal table */
    pth_syscall_libs = strdup(PTH_SYSCALL_LIBS);
    cpLib = pth_syscall_libs;
    for (i = 0; i < (sizeof(pth_syscall_lib_tab)/sizeof(pth_syscall_lib_tab_t))-1; ) {
        if ((cp = strchr(cpLib, ':')) != NULL)
            *cp++ = '\0';
        pth_syscall_lib_tab[i].path   = cpLib;
        pth_syscall_lib_tab[i].handle = NULL;
        i++;
        if (cp != NULL)
            cpLib = cp;
        else
            break;
    }
    pth_syscall_lib_tab[i].path = NULL;

#if defined(HAVE_DLOPEN) && defined(HAVE_DLSYM)
    /* determine addresses of syscall functions */
    for (i = 0; pth_syscall_fct_tab[i].name != NULL; i++) {

        /* attempt #1: fetch from implicit successor libraries */
#if defined(HAVE_DLSYM) && defined(HAVE_RTLD_NEXT)
        pth_syscall_fct_tab[i].addr = (pth_syscall_fct_t)
            dlsym(RTLD_NEXT, pth_syscall_fct_tab[i].name);
#endif

        /* attempt #2: fetch from explicitly loaded C library */
        if (pth_syscall_fct_tab[i].addr == NULL) {

            /* first iteration: try resolve from already loaded libraries */
            for (j = 0; pth_syscall_lib_tab[j].path != NULL; j++) {
                if (pth_syscall_lib_tab[j].handle != NULL) {
                    pth_syscall_fct_tab[i].addr = (pth_syscall_fct_t)
                        dlsym(pth_syscall_lib_tab[j].handle,
                              pth_syscall_fct_tab[i].name);
                    if (pth_syscall_fct_tab[i].addr != NULL)
                        break;
                }
            }

            /* second iteration: try to load more libraries for resolving */
            if (pth_syscall_fct_tab[i].addr == NULL) {
                for (j = 0; pth_syscall_lib_tab[j].path != NULL; j++) {
                    if (pth_syscall_lib_tab[j].handle == NULL) {
                        if ((pth_syscall_lib_tab[j].handle =
                             dlopen(pth_syscall_lib_tab[j].path, RTLD_LAZY)) == NULL)
                            continue;
                        pth_syscall_fct_tab[i].addr = (pth_syscall_fct_t)
                            dlsym(pth_syscall_lib_tab[j].handle,
                                  pth_syscall_fct_tab[i].name);
                        if (pth_syscall_fct_tab[i].addr != NULL)
                            break;
                    }
                }
            }
        }
    }
#endif
#endif
    return;
}

/* syscall wrapping initialization */
intern void pth_syscall_kill(void)
{
#if PTH_SYSCALL_HARD
    int i;

#if defined(HAVE_DLOPEN) && defined(HAVE_DLSYM)
    /* unload all explicitly loaded libraries */
    for (i = 0; pth_syscall_lib_tab[i].path != NULL; i++) {
        if (pth_syscall_lib_tab[i].handle != NULL) {
            dlclose(pth_syscall_lib_tab[i].handle);
            pth_syscall_lib_tab[i].handle = NULL;
        }
        pth_syscall_lib_tab[i].path = NULL;
    }
#endif
    free(pth_syscall_libs);
    pth_syscall_libs = NULL;
#endif
    return;
}

#if PTH_SYSCALL_HARD

/* utility macro for returning syscall errors */
#define PTH_SYSCALL_ERROR(return_val,errno_val,syscall) \
    do { fprintf(stderr, \
                 "pth:WARNING: unable to perform syscall `%s': " \
                 "no implementation resolvable\n", syscall); \
         errno = (errno_val); \
         return (return_val); \
    } while (0)

/* ==== Pth hard syscall wrapper for fork(2) ==== */
pid_t fork(void);
pid_t fork(void)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_fork();
}
intern pid_t pth_sc_fork(void)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_fork].addr != NULL)
        return ((pid_t (*)(void))
               pth_syscall_fct_tab[PTH_SCF_fork].addr)
               ();
#if defined(HAVE_SYSCALL) && defined(SYS_fork)
    else return (pid_t)syscall(SYS_fork);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "fork");
#endif
}

/* ==== Pth hard syscall wrapper for nanosleep(3) ==== */
int nanosleep(const struct timespec *, struct timespec *);
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_nanosleep(rqtp, rmtp);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_nanosleep necessary! */

/* ==== Pth hard syscall wrapper for usleep(3) ==== */
int usleep(unsigned int);
int usleep(unsigned int sec)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_usleep(sec);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_usleep necessary! */

/* ==== Pth hard syscall wrapper for sleep(3) ==== */
unsigned int sleep(unsigned int);
unsigned int sleep(unsigned int sec)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_sleep(sec);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_sleep necessary! */

/* ==== Pth hard syscall wrapper for system(3) ==== */
int system(const char *);
int system(const char *cmd)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_system(cmd);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_system necessary! */

/* ==== Pth hard syscall wrapper for sigprocmask(2) ==== */
int sigprocmask(int, const sigset_t *, sigset_t *);
int sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_sigmask(how, set, oset);
}
intern int pth_sc_sigprocmask(int how, const sigset_t *set, sigset_t *oset)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_sigprocmask].addr != NULL)
        return ((int (*)(int, const sigset_t *, sigset_t *))
               pth_syscall_fct_tab[PTH_SCF_sigprocmask].addr)
               (how, set, oset);
#if defined(HAVE_SYSCALL) && defined(SYS___sigprocmask14) /* NetBSD */
    else return (int)syscall(SYS___sigprocmask14, how, set, oset);
#elif defined(HAVE_SYSCALL) && defined(SYS_sigprocmask)
    else return (int)syscall(SYS_sigprocmask, how, set, oset);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "sigprocmask");
#endif
}

/* ==== Pth hard syscall wrapper for sigwait(3) ==== */
int sigwait(const sigset_t *, int *);
int sigwait(const sigset_t *set, int *sigp)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_sigwait(set, sigp);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_sigwait necessary! */

/* ==== Pth hard syscall wrapper for waitpid(2) ==== */
pid_t waitpid(pid_t, int *, int);
pid_t waitpid(pid_t wpid, int *status, int options)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_waitpid(wpid, status, options);
}
intern pid_t pth_sc_waitpid(pid_t wpid, int *status, int options)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_waitpid].addr != NULL)
        return ((pid_t (*)(pid_t, int *, int))
               pth_syscall_fct_tab[PTH_SCF_waitpid].addr)
               (wpid, status, options);
#if defined(HAVE_SYSCALL) && defined(SYS_waitpid)
    else return (pid_t)syscall(SYS_waitpid, wpid, status, options);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "waitpid");
#endif
}

/* ==== Pth hard syscall wrapper for connect(2) ==== */
int connect(int, const struct sockaddr *, socklen_t);
int connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_connect(s, addr, addrlen);
}
intern int pth_sc_connect(int s, const struct sockaddr *addr, socklen_t addrlen)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_connect].addr != NULL)
        return ((int (*)(int, const struct sockaddr *, socklen_t))
               pth_syscall_fct_tab[PTH_SCF_connect].addr)
               (s, addr, addrlen);
#if defined(HAVE_SYSCALL) && defined(SYS_connect)
    else return (int)syscall(SYS_connect, s, addr, addrlen);
#elif defined(HAVE_SYSCALL) && defined(SYS_socketcall) && defined(SOCKOP_connect) /* Linux */
    else {
        unsigned long args[3];
        args[0] = (unsigned long)s;
        args[1] = (unsigned long)addr;
        args[2] = (unsigned long)addrlen;
        return (int)syscall(SYS_socketcall, SOCKOP_connect, args);
    }
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "connect");
#endif
}

/* ==== Pth hard syscall wrapper for accept(2) ==== */
int accept(int, struct sockaddr *, socklen_t *);
int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_accept(s, addr, addrlen);
}
intern int pth_sc_accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_accept].addr != NULL)
        return ((int (*)(int, struct sockaddr *, socklen_t *))
               pth_syscall_fct_tab[PTH_SCF_accept].addr)
               (s, addr, addrlen);
#if defined(HAVE_SYSCALL) && defined(SYS_accept)
    else return (int)syscall(SYS_accept, s, addr, addrlen);
#elif defined(HAVE_SYSCALL) && defined(SYS_socketcall) && defined(SOCKOP_accept) /* Linux */
    else {
        unsigned long args[3];
        args[0] = (unsigned long)s;
        args[1] = (unsigned long)addr;
        args[2] = (unsigned long)addrlen;
        return (int)syscall(SYS_socketcall, SOCKOP_accept, args);
    }
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "accept");
#endif
}

/* ==== Pth hard syscall wrapper for select(2) ==== */
int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);
int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_select(nfds, readfds, writefds, exceptfds, timeout);
}
intern int pth_sc_select(int nfds, fd_set *readfds, fd_set *writefds,
                         fd_set *exceptfds, struct timeval *timeout)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_select].addr != NULL)
        return ((int (*)(int, fd_set *, fd_set *, fd_set *, struct timeval *))
               pth_syscall_fct_tab[PTH_SCF_select].addr)
               (nfds, readfds, writefds, exceptfds, timeout);
#if defined(HAVE_SYSCALL) && defined(SYS__newselect) /* Linux */
    else return (int)syscall(SYS__newselect, nfds, readfds, writefds, exceptfds, timeout);
#elif defined(HAVE_SYSCALL) && defined(SYS_select)
    else return (int)syscall(SYS_select, nfds, readfds, writefds, exceptfds, timeout);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "accept");
#endif
}

/* ==== Pth hard syscall wrapper for pselect(2) ==== */
int pselect(int, fd_set *, fd_set *, fd_set *, const struct timespec *, const sigset_t *);
int pselect(int nfds, fd_set *rfds, fd_set *wfds, fd_set *efds,
            const struct timespec *ts, const sigset_t *mask)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_pselect(nfds, rfds, wfds, efds, ts, mask);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_pselect necessary! */

/* ==== Pth hard syscall wrapper for poll(2) ==== */
int poll(struct pollfd *, nfds_t, int);
int poll(struct pollfd *pfd, nfds_t nfd, int timeout)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_poll(pfd, nfd, timeout);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_poll necessary! */

/* ==== Pth hard syscall wrapper for read(2) ==== */
ssize_t read(int, void *, size_t);
ssize_t read(int fd, void *buf, size_t nbytes)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_read(fd, buf, nbytes);
}
intern ssize_t pth_sc_read(int fd, void *buf, size_t nbytes)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_read].addr != NULL)
        return ((ssize_t (*)(int, void *, size_t))
               pth_syscall_fct_tab[PTH_SCF_read].addr)
               (fd, buf, nbytes);
#if defined(HAVE_SYSCALL) && defined(SYS_read)
    else return (ssize_t)syscall(SYS_read, fd, buf, nbytes);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "read");
#endif
}

/* ==== Pth hard syscall wrapper for write(2) ==== */
ssize_t write(int, const void *, size_t);
ssize_t write(int fd, const void *buf, size_t nbytes)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_write(fd, buf, nbytes);
}
intern ssize_t pth_sc_write(int fd, const void *buf, size_t nbytes)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_write].addr != NULL)
        return ((ssize_t (*)(int, const void *, size_t))
               pth_syscall_fct_tab[PTH_SCF_write].addr)
               (fd, buf, nbytes);
#if defined(HAVE_SYSCALL) && defined(SYS_write)
    else return (ssize_t)syscall(SYS_write, fd, buf, nbytes);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "write");
#endif
}

/* ==== Pth hard syscall wrapper for readv(2) ==== */
ssize_t readv(int, const struct iovec *, int);
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_readv(fd, iov, iovcnt);
}
intern ssize_t pth_sc_readv(int fd, const struct iovec *iov, int iovcnt)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_readv].addr != NULL)
        return ((ssize_t (*)(int, const struct iovec *, int))
               pth_syscall_fct_tab[PTH_SCF_readv].addr)
               (fd, iov, iovcnt);
#if defined(HAVE_SYSCALL) && defined(SYS_readv)
    else return (ssize_t)syscall(SYS_readv, fd, iov, iovcnt);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "readv");
#endif
}

/* ==== Pth hard syscall wrapper for writev(2) ==== */
ssize_t writev(int, const struct iovec *, int);
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_writev(fd, iov, iovcnt);
}
intern ssize_t pth_sc_writev(int fd, const struct iovec *iov, int iovcnt)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_writev].addr != NULL)
        return ((ssize_t (*)(int, const struct iovec *, int))
               pth_syscall_fct_tab[PTH_SCF_writev].addr)
               (fd, iov, iovcnt);
#if defined(HAVE_SYSCALL) && defined(SYS_writev)
    else return (ssize_t)syscall(SYS_writev, fd, iov, iovcnt);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "writev");
#endif
}

/* ==== Pth hard syscall wrapper for pread(2) ==== */
ssize_t pread(int, void *, size_t, off_t);
ssize_t pread(int fd, void *buf, size_t nbytes, off_t offset)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_pread(fd, buf, nbytes, offset);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_pread necessary! */

/* ==== Pth hard syscall wrapper for pwrite(2) ==== */
ssize_t pwrite(int, const void *, size_t, off_t);
ssize_t pwrite(int fd, const void *buf, size_t nbytes, off_t offset)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_pwrite(fd, buf, nbytes, offset);
}
/* NOTICE: internally fully emulated, so still no
   internal exit point pth_sc_pwrite necessary! */

/* ==== Pth hard syscall wrapper for recv(2) ==== */
ssize_t recv(int, void *, size_t, int);
ssize_t recv(int fd, void *buf, size_t nbytes, int flags)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_recv(fd, buf, nbytes, flags);
}
intern ssize_t pth_sc_recv(int fd, void *buf, size_t nbytes, int flags)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_recv].addr != NULL)
        return ((ssize_t (*)(int, void *, size_t, int))
               pth_syscall_fct_tab[PTH_SCF_recv].addr)
               (fd, buf, nbytes, flags);
#if defined(HAVE_SYSCALL) && defined(SYS_recv)
    else return (ssize_t)syscall(SYS_recv, fd, buf, nbytes, flags);
#elif defined(HAVE_SYSCALL) && defined(SYS_recvfrom)
    else return (ssize_t)syscall(SYS_recvfrom, fd, buf, nbytes, flags, (struct sockaddr *)NULL, (socklen_t *)NULL);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "recv");
#endif
}

/* ==== Pth hard syscall wrapper for send(2) ==== */
ssize_t send(int, void *, size_t, int);
ssize_t send(int fd, void *buf, size_t nbytes, int flags)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_send(fd, buf, nbytes, flags);
}
intern ssize_t pth_sc_send(int fd, void *buf, size_t nbytes, int flags)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_send].addr != NULL)
        return ((ssize_t (*)(int, void *, size_t, int))
               pth_syscall_fct_tab[PTH_SCF_send].addr)
               (fd, buf, nbytes, flags);
#if defined(HAVE_SYSCALL) && defined(SYS_send)
    else return (ssize_t)syscall(SYS_send, fd, buf, nbytes, flags);
#elif defined(HAVE_SYSCALL) && defined(SYS_sendto)
    else return (ssize_t)syscall(SYS_sendto, fd, buf, nbytes, flags, (struct sockaddr *)NULL, 0);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "send");
#endif
}

/* ==== Pth hard syscall wrapper for recvfrom(2) ==== */
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
ssize_t recvfrom(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_recvfrom(fd, buf, nbytes, flags, from, fromlen);
}
intern ssize_t pth_sc_recvfrom(int fd, void *buf, size_t nbytes, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_recvfrom].addr != NULL)
        return ((ssize_t (*)(int, void *, size_t, int, struct sockaddr *, socklen_t *))
               pth_syscall_fct_tab[PTH_SCF_recvfrom].addr)
               (fd, buf, nbytes, flags, from, fromlen);
#if defined(HAVE_SYSCALL) && defined(SYS_recvfrom)
    else return (ssize_t)syscall(SYS_recvfrom, fd, buf, nbytes, flags, from, fromlen);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "recvfrom");
#endif
}

/* ==== Pth hard syscall wrapper for sendto(2) ==== */
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t sendto(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen)
{
    /* external entry point for application */
    pth_implicit_init();
    return pth_sendto(fd, buf, nbytes, flags, to, tolen);
}
intern ssize_t pth_sc_sendto(int fd, const void *buf, size_t nbytes, int flags, const struct sockaddr *to, socklen_t tolen)
{
    /* internal exit point for Pth */
    if (pth_syscall_fct_tab[PTH_SCF_sendto].addr != NULL)
        return ((ssize_t (*)(int, const void *, size_t, int, const struct sockaddr *, socklen_t))
               pth_syscall_fct_tab[PTH_SCF_sendto].addr)
               (fd, buf, nbytes, flags, to, tolen);
#if defined(HAVE_SYSCALL) && defined(SYS_sendto)
    else return (ssize_t)syscall(SYS_sendto, fd, buf, nbytes, flags, to, tolen);
#else
    else PTH_SYSCALL_ERROR(-1, ENOSYS, "sendto");
#endif
}

#endif /* PTH_SYSCALL_HARD */

