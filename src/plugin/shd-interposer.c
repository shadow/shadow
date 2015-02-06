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
#include <malloc.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
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

#include "shadow.h"

#define SETSYM(funcptr, funcstr) {funcptr = dlsym(RTLD_NEXT, funcstr);}

#define SETSYM_OR_FAIL(funcptr, funcstr) { \
    dlerror(); \
    SETSYM(funcptr, funcstr);\
    dlerror(); \
    funcptr = dlsym(RTLD_NEXT, funcstr); \
    char* errorMessage = dlerror(); \
    if(errorMessage != NULL) { \
        fprintf(stderr, "dlsym(%s): dlerror(): %s\n", funcstr, errorMessage); \
        exit(EXIT_FAILURE); \
    } else if(funcptr == NULL) { \
        fprintf(stderr, "dlsym(%s): returned NULL pointer\n", funcstr); \
        exit(EXIT_FAILURE); \
    } \
}

#define ENSURE(type, prefix, func) { \
    if(!director.type.func) { \
        SETSYM_OR_FAIL(director.type.func, prefix #func); \
    } \
}

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
typedef int (*eventfd_func)(unsigned int, int);

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
typedef size_t (*pread_func)(int, void*, size_t, off_t);
typedef int (*flock_func)(int, int);
typedef int (*fsync_func)(int);
typedef int (*ftruncate_func)(int, int);
typedef int (*posix_fallocate_func)(int, int, int);

/* time family */

typedef time_t (*time_func)(time_t*);
typedef int (*clock_gettime_func)(clockid_t, struct timespec *);
typedef int (*gettimeofday_func)(struct timeval*, __timezone_ptr_t);

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

typedef int (*on_exit_func)(void (*function)(int , void *), void *arg);
typedef int (*atexit_func)(void (*func)(void));
typedef int (*__cxa_atexit_func)(void (*func) (void *), void * arg, void * dso_handle);

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
    eventfd_func eventfd;

    fileno_func fileno;
    open_func open;
    open64_func open64;
    creat_func creat;
    fopen_func fopen;
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
    pread_func pread;
    flock_func flock;
    fsync_func fsync;
    ftruncate_func ftruncate;
    posix_fallocate_func posix_fallocate;

    time_func time;
    clock_gettime_func clock_gettime;
    gettimeofday_func gettimeofday;

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

    on_exit_func on_exit;
    atexit_func atexit;
    __cxa_atexit_func __cxa_atexit;
} PreloadFuncs;

typedef struct {
    struct {
        char buf[102400];
        size_t pos;
        size_t nallocs;
        size_t ndeallocs;
    } dummy;
    PreloadFuncs libc;
    gboolean shadowIsLoaded;
} FuncDirector;

/* global storage for function pointers that we look up lazily */
static FuncDirector director;

/* track if we are in a recursive loop to avoid infinite recursion.
 * threads MUST access this via &isRecursive to ensure each has its own copy
 * http://gcc.gnu.org/onlinedocs/gcc-4.3.6/gcc/Thread_002dLocal.html */
static __thread unsigned long isRecursive = 0;

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
    director.shadowIsLoaded = TRUE;
}

static void _interposer_globalInitialize() {
    /* ensure we never intercept during initialization */
    __sync_fetch_and_add(&isRecursive, 1);

    memset(&director, 0, sizeof(FuncDirector));

    /* use dummy malloc during initial dlsym calls to avoid recursive stack segfaults */
    director.libc.malloc = dummy_malloc;
    director.libc.calloc = dummy_calloc;
    director.libc.free = dummy_free;

    malloc_func tempMalloc;
    calloc_func tempCalloc;
    free_func tempFree;

    SETSYM_OR_FAIL(tempMalloc, "malloc");
    SETSYM_OR_FAIL(tempCalloc, "calloc");
    SETSYM_OR_FAIL(tempFree, "free");

    /* stop using the dummy malloc funcs now */
    director.libc.malloc = tempMalloc;
    director.libc.calloc = tempCalloc;
    director.libc.free = tempFree;

    /* lookup the remaining functions */
    SETSYM_OR_FAIL(director.libc.realloc, "realloc");
    SETSYM_OR_FAIL(director.libc.posix_memalign, "posix_memalign");
    SETSYM_OR_FAIL(director.libc.memalign, "memalign");
    SETSYM_OR_FAIL(director.libc.valloc, "valloc");
    SETSYM_OR_FAIL(director.libc.pvalloc, "pvalloc");
    SETSYM_OR_FAIL(director.libc.mmap, "mmap");
    SETSYM_OR_FAIL(director.libc.epoll_create, "epoll_create");
    SETSYM_OR_FAIL(director.libc.epoll_create1, "epoll_create1");
    SETSYM_OR_FAIL(director.libc.epoll_ctl, "epoll_ctl");
    SETSYM_OR_FAIL(director.libc.epoll_wait, "epoll_wait");
    SETSYM_OR_FAIL(director.libc.epoll_pwait, "epoll_pwait");
    SETSYM_OR_FAIL(director.libc.timerfd_create, "timerfd_create");
    SETSYM_OR_FAIL(director.libc.timerfd_settime, "timerfd_settime");
    SETSYM_OR_FAIL(director.libc.timerfd_gettime, "timerfd_gettime");
    SETSYM_OR_FAIL(director.libc.socket, "socket");
    SETSYM_OR_FAIL(director.libc.socketpair, "socketpair");
    SETSYM_OR_FAIL(director.libc.bind, "bind");
    SETSYM_OR_FAIL(director.libc.getsockname, "getsockname");
    SETSYM_OR_FAIL(director.libc.connect, "connect");
    SETSYM_OR_FAIL(director.libc.getpeername, "getpeername");
    SETSYM_OR_FAIL(director.libc.send, "send");
    SETSYM_OR_FAIL(director.libc.sendto, "sendto");
    SETSYM_OR_FAIL(director.libc.sendmsg, "sendmsg");
    SETSYM_OR_FAIL(director.libc.recv, "recv");
    SETSYM_OR_FAIL(director.libc.recvfrom, "recvfrom");
    SETSYM_OR_FAIL(director.libc.recvmsg, "recvmsg");
    SETSYM_OR_FAIL(director.libc.getsockopt, "getsockopt");
    SETSYM_OR_FAIL(director.libc.setsockopt, "setsockopt");
    SETSYM_OR_FAIL(director.libc.listen, "listen");
    SETSYM_OR_FAIL(director.libc.accept, "accept");
    SETSYM_OR_FAIL(director.libc.accept4, "accept4");
    SETSYM_OR_FAIL(director.libc.shutdown, "shutdown");
    SETSYM_OR_FAIL(director.libc.pipe, "pipe");
    SETSYM_OR_FAIL(director.libc.pipe2, "pipe2");
    SETSYM_OR_FAIL(director.libc.read, "read");
    SETSYM_OR_FAIL(director.libc.write, "write");
    SETSYM_OR_FAIL(director.libc.readv, "readv");
    SETSYM_OR_FAIL(director.libc.writev, "writev");
    SETSYM_OR_FAIL(director.libc.close, "close");
    SETSYM_OR_FAIL(director.libc.fcntl, "fcntl");
    SETSYM_OR_FAIL(director.libc.ioctl, "ioctl");
    SETSYM_OR_FAIL(director.libc.eventfd, "eventfd");
    SETSYM_OR_FAIL(director.libc.fileno, "fileno");
    SETSYM_OR_FAIL(director.libc.open, "open");
    SETSYM_OR_FAIL(director.libc.open64, "open64");
    SETSYM_OR_FAIL(director.libc.creat, "creat");
    SETSYM_OR_FAIL(director.libc.fopen, "fopen");
    SETSYM_OR_FAIL(director.libc.fdopen, "fdopen");
    SETSYM_OR_FAIL(director.libc.dup, "dup");
    SETSYM_OR_FAIL(director.libc.dup2, "dup2");
    SETSYM_OR_FAIL(director.libc.dup3, "dup3");
    SETSYM_OR_FAIL(director.libc.fclose, "fclose");
    SETSYM_OR_FAIL(director.libc.__fxstat, "__fxstat");
    SETSYM_OR_FAIL(director.libc.__fxstat64, "__fxstat64");
    SETSYM_OR_FAIL(director.libc.fstatfs, "fstatfs");
    SETSYM_OR_FAIL(director.libc.lseek, "lseek");
    SETSYM_OR_FAIL(director.libc.pread, "pread");
    SETSYM_OR_FAIL(director.libc.flock, "flock");
    SETSYM_OR_FAIL(director.libc.fsync, "fsync");
    SETSYM_OR_FAIL(director.libc.ftruncate, "ftruncate");
    SETSYM_OR_FAIL(director.libc.posix_fallocate, "posix_fallocate");
    SETSYM_OR_FAIL(director.libc.time, "time");
    SETSYM_OR_FAIL(director.libc.clock_gettime, "clock_gettime");
    SETSYM_OR_FAIL(director.libc.gettimeofday, "gettimeofday");
    SETSYM_OR_FAIL(director.libc.gethostname, "gethostname");
    SETSYM_OR_FAIL(director.libc.getaddrinfo, "getaddrinfo");
    SETSYM_OR_FAIL(director.libc.freeaddrinfo, "freeaddrinfo");
    SETSYM_OR_FAIL(director.libc.getnameinfo, "getnameinfo");
    SETSYM_OR_FAIL(director.libc.gethostbyname, "gethostbyname");
    SETSYM_OR_FAIL(director.libc.gethostbyname_r, "gethostbyname_r");
    SETSYM_OR_FAIL(director.libc.gethostbyname2, "gethostbyname2");
    SETSYM_OR_FAIL(director.libc.gethostbyname2_r, "gethostbyname2_r");
    SETSYM_OR_FAIL(director.libc.gethostbyaddr, "gethostbyaddr");
    SETSYM_OR_FAIL(director.libc.gethostbyaddr_r, "gethostbyaddr_r");
    SETSYM_OR_FAIL(director.libc.rand, "rand");
    SETSYM_OR_FAIL(director.libc.rand_r, "rand_r");
    SETSYM_OR_FAIL(director.libc.srand, "srand");
    SETSYM_OR_FAIL(director.libc.random, "random");
    SETSYM_OR_FAIL(director.libc.random_r, "random_r");
    SETSYM_OR_FAIL(director.libc.srandom, "srandom");
    SETSYM_OR_FAIL(director.libc.srandom_r, "srandom_r");
    SETSYM_OR_FAIL(director.libc.on_exit, "on_exit");
    SETSYM_OR_FAIL(director.libc.__cxa_atexit, "__cxa_atexit");

    /* attempt lookup but don't fail as its valid not to exist */
    SETSYM(director.libc.atexit, "atexit");
    SETSYM(director.libc.aligned_alloc, "aligned_alloc");

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

static inline int shouldForwardToLibC() {
    int useLibC = 1;
    /* recursive calls always go to libc */
    if(!__sync_fetch_and_add(&isRecursive, 1)) {
        Thread* thread = director.shadowIsLoaded && worker_isAlive() ? worker_getActiveThread() : NULL;
        /* check if the shadow intercept library is loaded yet, but dont fail if its not */
        if(thread) {
            /* ask shadow if this call is a plug-in that should be intercepted */
            useLibC = thread_shouldInterpose(thread) ? 0 : 1;
        } else {
            /* intercept library is not yet loaded, don't redirect */
            useLibC = 1;
        }
    }
    __sync_fetch_and_sub(&isRecursive, 1);
    return useLibC;
}

enum SystemCallType {
    SCT_BIND, SCT_CONNECT, SCT_GETSOCKNAME, SCT_GETPEERNAME,
};

static Host* _interposer_switchInShadowContext() {
    Thread* thread = worker_getActiveThread();
    if(thread) {
        thread_beginControl(thread);
    }
    return worker_getCurrentHost();
}

static void _interposer_switchOutShadowContext(Host* node) {
    Thread* thread = worker_getActiveThread();
    if(thread) {
        thread_endControl(thread);
    }
}

static gint _interposer_addressHelper(gint fd, const struct sockaddr* addr, socklen_t* len,
        enum SystemCallType type) {
    Host* host = _interposer_switchInShadowContext();
    gint result = 0;

    /* check if this is a virtual socket */
    if(!host_isShadowDescriptor(host, fd)){
        warning("intercepted a non-virtual descriptor");
        result = EBADF;
    } else if(addr == NULL) { /* check for proper addr */
        result = EFAULT;
    } else if(len == NULL) {
        result = EINVAL;
    }

    if(result == 0) {
        /* direct to node for further checks */
        switch(type) {
            case SCT_BIND: {
                result = host_bindToInterface(host, fd, addr);
                break;
            }

            case SCT_CONNECT: {
                result = host_connectToPeer(host, fd, addr);
                break;
            }

            case SCT_GETPEERNAME:
            case SCT_GETSOCKNAME: {
                result = type == SCT_GETPEERNAME ?
                        host_getPeerName(host, fd, addr, len) :
                        host_getSocketName(host, fd, addr, len);
                break;
            }

            default: {
                result = EINVAL;
                error("unrecognized system call type");
                break;
            }
        }
    }

    _interposer_switchOutShadowContext(host);

    /* check if there was an error */
    if(result != 0) {
        errno = result;
        return -1;
    }

    return 0;
}

static gssize _interposer_sendHelper(Host* host, gint fd, gconstpointer buf, gsize n, gint flags,
        const struct sockaddr* addr, socklen_t len) {
    /* this function MUST be called after switching in shadow context */
    /* TODO flags are ignored */
    /* make sure this is a socket */
    if(!host_isShadowDescriptor(host, fd)){
        errno = EBADF;
        return -1;
    }

    in_addr_t ip = 0;
    in_port_t port = 0;

    /* check if they specified an address to send to */
    if(addr != NULL && len >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in* si = (struct sockaddr_in*) addr;
        ip = si->sin_addr.s_addr;
        port = si->sin_port;
    }

    gsize bytes = 0;
    gint result = host_sendUserData(host, fd, buf, n, ip, port, &bytes);

    if(result != 0) {
        errno = result;
        return -1;
    }
    return (gssize) bytes;
}

static gssize _interposer_recvHelper(Host* host, gint fd, gpointer buf, size_t n, gint flags,
        struct sockaddr* addr, socklen_t* len) {
    /* this function MUST be called after switching in shadow context */
    /* TODO flags are ignored */
    /* make sure this is a socket */
    if(!host_isShadowDescriptor(host, fd)){
        errno = EBADF;
        return -1;
    }

    in_addr_t ip = 0;
    in_port_t port = 0;

    gsize bytes = 0;
    gint result = host_receiveUserData(host, fd, buf, n, &ip, &port, &bytes);

    if(result != 0) {
        errno = result;
        return -1;
    }

    /* check if they wanted to know where we got the data from */
    if(addr != NULL && len != NULL && *len >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in* si = (struct sockaddr_in*) addr;
        si->sin_addr.s_addr = ip;
        si->sin_port = port;
        si->sin_family = AF_INET;
        *len = sizeof(struct sockaddr_in);
    }

    return (gssize) bytes;
}

static gint _interposer_fcntlHelper(int fd, int cmd, va_list farg) {
    /* check if this is a socket */
    Host* node = _interposer_switchInShadowContext();

    if(!host_isShadowDescriptor(node, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = fcntl(osfd, cmd, va_arg(farg, void*));
        } else {
            errno = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        return ret;
    }

    /* normally, the type of farg depends on the cmd */
    Descriptor* descriptor = host_lookupDescriptor(node, fd);

    gint result = 0;
    if(descriptor) {
        if (cmd == F_GETFL) {
            result = descriptor_getFlags(descriptor);
        } else if (cmd == F_SETFL) {
            descriptor_setFlags(descriptor, va_arg(farg, int));
        }
    } else {
        errno = EBADF;
        result = -1;
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

static int _interposer_fcntl(int fd, int cmd, ...) {
    va_list farg;
    va_start(farg, cmd);
    int result = _interposer_fcntlHelper(fd, cmd, farg);
    va_end(farg);
    return result;
}

static gint _interposer_ioctlHelper(int fd, unsigned long int request, va_list farg) {
    /* check if this is a socket */
    Host* node = _interposer_switchInShadowContext();

    if(!host_isShadowDescriptor(node, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = ioctl(fd, request, farg);
        } else {
            errno = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        return ret;
    }

    gint result = 0;

    /* normally, the type of farg depends on the request */
    Descriptor* descriptor = host_lookupDescriptor(node, fd);

    if(descriptor) {
        DescriptorType t = descriptor_getType(descriptor);
        if(t == DT_TCPSOCKET || t == DT_UDPSOCKET) {
            Socket* socket = (Socket*) descriptor;
            if(request == SIOCINQ || request == FIONREAD) {
                gsize bufferLength = socket_getInputBufferLength(socket);
                gint* lengthOut = va_arg(farg, int*);
                *lengthOut = (gint)bufferLength;
            } else if (request == SIOCOUTQ || request == TIOCOUTQ) {
                gsize bufferLength = socket_getOutputBufferLength(socket);
                gint* lengthOut = va_arg(farg, int*);
                *lengthOut = (gint)bufferLength;
            } else {
                result = ENOTTY;
            }
        } else {
            result = ENOTTY;
        }
    } else {
        result = EBADF;
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

static int _interposer_ioctl(int fd, unsigned long int request, ...) {
    va_list farg;
    va_start(farg, request);
    int result = _interposer_ioctlHelper(fd, request, farg);
    va_end(farg);
    return result;
}

/****************************************************************************
 * Preloaded functions that switch execution control from the plug-in program
 * back to Shadow
 ****************************************************************************/

/* memory allocation family */

void* malloc(size_t size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", malloc);
        return director.libc.malloc(size);
    }

    Host* node = _interposer_switchInShadowContext();
    void* ptr = malloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, size);
    }
    _interposer_switchOutShadowContext(node);

    return ptr;
}

void* calloc(size_t nmemb, size_t size) {
    if(shouldForwardToLibC()) {
        /* the dlsym lookup for calloc may call calloc again, causing infinite recursion */
        if(!director.libc.calloc) {
            /* make sure to use dummy_calloc when looking up calloc */
            director.libc.calloc = dummy_calloc;
            /* this will set director.real.calloc to the correct calloc */
            ENSURE(libc, "", calloc);
        }
        return director.libc.calloc(nmemb, size);
    }

    Host* node = _interposer_switchInShadowContext();
    void* ptr = calloc(nmemb, size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, size);
    }
    _interposer_switchOutShadowContext(node);
    return ptr;
}

void* realloc(void *ptr, size_t size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", realloc);
        return director.libc.realloc(ptr, size);
    }

    Host* node = _interposer_switchInShadowContext();

    gpointer newptr = realloc(ptr, size);
    if(newptr != NULL) {
        if(ptr == NULL) {
            /* equivalent to malloc */
            if(size) {
                tracker_addAllocatedBytes(host_getTracker(node), newptr, size);
            }
        } else if (size == 0) {
            /* equivalent to free */
            tracker_removeAllocatedBytes(host_getTracker(node), ptr);
        } else {
            /* true realloc */
            tracker_removeAllocatedBytes(host_getTracker(node), ptr);
            if(size) {
                tracker_addAllocatedBytes(host_getTracker(node), newptr, size);
            }
        }
    }

    _interposer_switchOutShadowContext(node);
    return newptr;
}

void free(void *ptr) {
    if(shouldForwardToLibC()) {
        /* check if the ptr is in the dummy buf, and free it using the dummy free func */
        void* dummyBufStart = &(director.dummy.buf[0]);
        void* dummyBufEnd = &(director.dummy.buf[sizeof(director.dummy.buf)-1]);

        if (ptr >= dummyBufStart && ptr <= dummyBufEnd) {
            dummy_free(ptr);
            return;
        }

        ENSURE(libc, "", free);
        director.libc.free(ptr);
        return;
    }

    Host* node = _interposer_switchInShadowContext();
    free(ptr);
    if(ptr != NULL) {
        tracker_removeAllocatedBytes(host_getTracker(node), ptr);
    }
    _interposer_switchOutShadowContext(node);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", posix_memalign);
        return director.libc.posix_memalign(memptr, alignment, size);
    }

    Host* node = _interposer_switchInShadowContext();
    gint ret = posix_memalign(memptr, alignment, size);
    if(ret == 0 && size) {
        tracker_addAllocatedBytes(host_getTracker(node), *memptr, size);
    }
    _interposer_switchOutShadowContext(node);
    return ret;
}

void* memalign(size_t blocksize, size_t bytes) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", memalign);
        return director.libc.memalign(blocksize, bytes);
    }

    Host* node = _interposer_switchInShadowContext();
    gpointer ptr = memalign(blocksize, bytes);
    if(bytes && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, bytes);
    }
    _interposer_switchOutShadowContext(node);
    return ptr;
}

/* aligned_alloc doesnt exist in glibc in the current LTS version of ubuntu */
void* aligned_alloc(size_t alignment, size_t size) {
    if(shouldForwardToLibC()) {
        /* if they called it, it better exist... */
        ENSURE(libc, "", aligned_alloc);
        return director.libc.aligned_alloc(alignment, size);
    }

    Host* node = _interposer_switchInShadowContext();
    gpointer ptr = aligned_alloc(alignment, size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, size);
    }
    _interposer_switchOutShadowContext(node);
    return ptr;
}

void* valloc(size_t size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", valloc);
        return director.libc.valloc(size);
    }

    Host* node = _interposer_switchInShadowContext();
    gpointer ptr = valloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, size);
    }
    _interposer_switchOutShadowContext(node);
    return ptr;
}

void* pvalloc(size_t size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", pvalloc);
        return director.libc.pvalloc(size);
    }

    Host* node = _interposer_switchInShadowContext();
    gpointer ptr = pvalloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(node), ptr, size);
    }
    _interposer_switchOutShadowContext(node);
    return ptr;
}

/* for fd translation */
void* mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", mmap);
        return director.libc.mmap(addr, length, prot, flags, fd, offset);
    }

    Host* host = _interposer_switchInShadowContext();

    /* anonymous mappings ignore file descriptor */
    if(flags & MAP_ANONYMOUS) {
        gpointer ret = mmap(addr, length, prot, flags, -1, offset);
        _interposer_switchOutShadowContext(host);
        return ret;
    }

    if (host_isShadowDescriptor(host, fd)) {
        warning("mmap not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gpointer ret = mmap(addr, length, prot, flags, osfd, offset);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;

    return MAP_FAILED;
}


/* event family */

int epoll_create(int size) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", epoll_create);
        return director.libc.epoll_create(size);
    }

    /* size should be > 0, but can otherwise be completely ignored */
    if(size < 1) {
        errno = EINVAL;
        return -1;
    }

    /* switch into shadow and create the new descriptor */
    Host* node = _interposer_switchInShadowContext();
    gint handle = host_createDescriptor(node, DT_EPOLL);
    _interposer_switchOutShadowContext(node);

    return handle;
}

int epoll_create1(int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", epoll_create1);
        return director.libc.epoll_create1(flags);
    }

    /*
     * the only possible flag is EPOLL_CLOEXEC, which means we should set
     * FD_CLOEXEC on the new file descriptor. just ignore for now.
     */
    if(flags != 0 && flags != EPOLL_CLOEXEC) {
        errno = EINVAL;
        return -1;
    }

    /* forward on to our regular create method */
    return epoll_create(1);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", epoll_ctl);
        return director.libc.epoll_ctl(epfd, op, fd, event);
    }

    /*
     * initial checks before passing on to node:
     * EINVAL if fd is the same as epfd, or the requested operation op is not
     * supported by this interface
     */
    if(epfd == fd) {
        errno = EINVAL;
        return -1;
    }

    /* switch into shadow and do the operation */
    Host* node = _interposer_switchInShadowContext();
    gint result = host_epollControl(node, epfd, op, fd, event);
    _interposer_switchOutShadowContext(node);

    /*
     * When successful, epoll_ctl() returns zero. When an error occurs,
     * epoll_ctl() returns -1 and errno is set appropriately.
     */
    if(result != 0) {
        errno = result;
        return -1;
    } else {
        return 0;
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", epoll_wait);
        return director.libc.epoll_wait(epfd, events, maxevents, timeout);
    }

    /*
     * EINVAL if maxevents is less than or equal to zero.
     */
    if(maxevents <= 0) {
        errno = EINVAL;
        return -1;
    }

    /* switch to shadow context and try to get events if we have any */
    Host* node = _interposer_switchInShadowContext();

    /*
     * initial checks: we can't block, so timeout must be 0. anything else will
     * cause a warning. if they seriously want to block by passing in -1, then
     * return interrupt below only if we have no events.
     *
     * @note log while in shadow context to get node info in the log
     */
    if(timeout != 0) {
        warning("Shadow does not block, so the '%i' millisecond timeout will be ignored", timeout);
    }

    gint nEvents = 0;
    gint result = host_epollGetEvents(node, epfd, events, maxevents, &nEvents);
    _interposer_switchOutShadowContext(node);

    /* check if there was an error */
    if(result != 0) {
        errno = result;
        return -1;
    }

    /*
     * if we dont have any events and they are trying to block, tell them their
     * timeout was interrupted.
     */
    if(timeout != 0 && nEvents <= 0) {
        errno = EINTR;
        return -1;
    }

    /* the event count. zero is fine since they weren't expecting a timer. */
    return nEvents;
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", epoll_pwait);
        return director.libc.epoll_pwait(epfd, events, maxevents, timeout, ss);
    }

    /*
     * this is the same as epoll_wait, except it catches signals in the
     * signal set. lets just assume we have no signals to worry about.
     * forward to our regular wait method.
     *
     * @warning we dont handle signals
     */
    if(ss) {
        Host* node = _interposer_switchInShadowContext();
        warning("epollpwait using a signalset is not yet supported");
        _interposer_switchOutShadowContext(node);
    }
    return epoll_wait(epfd, events, maxevents, timeout);
}

/* socket/io family */

int socket(int domain, int type, int protocol) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", socket);
        return director.libc.socket(domain, type, protocol);
    }

    /* we only support non-blocking sockets, and require
     * SOCK_NONBLOCK to be set immediately */
    gboolean isBlocking = FALSE;

    /* clear non-blocking flags if set to get true type */
    if(type & SOCK_NONBLOCK) {
        type = type & ~SOCK_NONBLOCK;
        isBlocking = FALSE;
    }
    if(type & SOCK_CLOEXEC) {
        type = type & ~SOCK_CLOEXEC;
        isBlocking = FALSE;
    }

    gint result = 0;
    Host* node = _interposer_switchInShadowContext();

    /* check inputs for what we support */
    if(isBlocking) {
        warning("we only support non-blocking sockets: please bitwise OR 'SOCK_NONBLOCK' with type flags");
        errno = EPROTONOSUPPORT;
        result = -1;
    } else if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM", type);
        errno = EPROTONOSUPPORT;
        result = -1;
    } else if(domain != AF_INET && domain != AF_UNIX) {
        warning("trying to create socket with domain \"%i\", we only support AF_INET and AF_UNIX", domain);
        errno = EAFNOSUPPORT;
        result = -1;
    }

    if(result == 0) {
        /* we are all set to create the socket */
        DescriptorType dtype = type == SOCK_STREAM ? DT_TCPSOCKET : DT_UDPSOCKET;
        result = host_createDescriptor(node, dtype);
        if(domain == AF_UNIX) {
            Socket* s = (Socket*)host_lookupDescriptor(node, result);
            socket_setUnix(s, TRUE);
        }
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", socketpair);
        return director.libc.socketpair(domain, type, protocol, fds);
    }

    /* create a pair of connected sockets, i.e. a bi-directional pipe */
    if(domain != AF_UNIX) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    /* only support non-blocking sockets */
    gboolean isBlocking = FALSE;

    /* clear non-blocking flags if set to get true type */
    gint realType = type;
    if(realType & SOCK_NONBLOCK) {
        realType = realType & ~SOCK_NONBLOCK;
        isBlocking = FALSE;
    }
    if(realType & SOCK_CLOEXEC) {
        realType = realType & ~SOCK_CLOEXEC;
        isBlocking = FALSE;
    }

    if(realType != SOCK_STREAM) {
        errno = EPROTONOSUPPORT;
        return -1;
    }

    gint result = 0;
    Host* node = _interposer_switchInShadowContext();

    if(isBlocking) {
        warning("we only support non-blocking sockets: please bitwise OR 'SOCK_NONBLOCK' with type flags");
        errno = EPROTONOSUPPORT;
        result = -1;
    }

    if(result == 0) {
        gint handle = host_createDescriptor(node, DT_SOCKETPAIR);

        Channel* channel = (Channel*) host_lookupDescriptor(node, handle);
        gint linkedHandle = channel_getLinkedHandle(channel);

        fds[0] = handle;
        fds[1] = linkedHandle;
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

int bind(int fd, const struct sockaddr* addr, socklen_t len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", bind);
        return director.libc.bind(fd, addr, len);
    }

    if((addr->sa_family == AF_INET && len < sizeof(struct sockaddr_in)) ||
            (addr->sa_family == AF_UNIX && len < sizeof(struct sockaddr_un))) {
        return EINVAL;
    }

    return _interposer_addressHelper(fd, addr, &len, SCT_BIND);
}

int getsockname(int fd, struct sockaddr* addr, socklen_t* len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getsockname);
        return director.libc.getsockname(fd, addr, len);
    }

    return _interposer_addressHelper(fd, addr, len, SCT_GETSOCKNAME);
}

int connect(int fd, const struct sockaddr* addr, socklen_t len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", connect);
        return director.libc.connect(fd, addr, len);
    }

    if((addr->sa_family == AF_INET && len < sizeof(struct sockaddr_in)) ||
            (addr->sa_family == AF_UNIX && len < sizeof(struct sockaddr_un))) {
        return EINVAL;
    }

    return _interposer_addressHelper(fd, addr, &len, SCT_CONNECT);
}

int getpeername(int fd, struct sockaddr* addr, socklen_t* len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getpeername);
        return director.libc.getpeername(fd, addr, len);
    }

    return _interposer_addressHelper(fd, addr, len, SCT_GETPEERNAME);
}

ssize_t send(int fd, const void *buf, size_t n, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", send);
        return director.libc.send(fd, buf, n, flags);
    }

    Host* host = _interposer_switchInShadowContext();
    gssize result = _interposer_sendHelper(host, fd, buf, n, flags, NULL, 0);
    _interposer_switchOutShadowContext(host);
    return result;
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags, const struct sockaddr* addr, socklen_t addr_len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", sendto);
        return director.libc.sendto(fd, buf, n, flags, addr, addr_len);
    }

    Host* host = _interposer_switchInShadowContext();
    gssize result = _interposer_sendHelper(host, fd, buf, n, flags, addr, addr_len);
    _interposer_switchOutShadowContext(host);
    return result;
}

ssize_t sendmsg(int fd, const struct msghdr *message, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", sendmsg);
        return director.libc.sendmsg(fd, message, flags);
    }

    /* TODO implement */
    Host* node = _interposer_switchInShadowContext();
    warning("sendmsg not implemented");
    _interposer_switchOutShadowContext(node);
    errno = ENOSYS;
    return -1;
}

ssize_t recv(int fd, void *buf, size_t n, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", recv);
        return director.libc.recv(fd, buf, n, flags);
    }

    Host* host = _interposer_switchInShadowContext();
    gssize result = _interposer_recvHelper(host, fd, buf, n, flags, NULL, 0);
    _interposer_switchOutShadowContext(host);
    return result;
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, struct sockaddr* addr, socklen_t *restrict addr_len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", recvfrom);
        return director.libc.recvfrom(fd, buf, n, flags, addr, addr_len);
    }

    Host* host = _interposer_switchInShadowContext();
    gssize result = _interposer_recvHelper(host, fd, buf, n, flags, addr, addr_len);
    _interposer_switchOutShadowContext(host);
    return result;
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", recvmsg);
        return director.libc.recvmsg(fd, message, flags);
    }

    /* TODO implement */
    Host* node = _interposer_switchInShadowContext();
    warning("recvmsg not implemented");
    _interposer_switchOutShadowContext(node);
    errno = ENOSYS;
    return -1;
}

int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getsockopt);
        return director.libc.getsockopt(fd, level, optname, optval, optlen);
    }

    if(!optlen) {
        errno = EFAULT;
        return -1;
    }

    Host* node = _interposer_switchInShadowContext();
    Descriptor* descriptor = host_lookupDescriptor(node, fd);

    gint result = 0;

    /* TODO: implement socket options */
    if(descriptor) {
        if(level == SOL_SOCKET || level == SOL_IP || level == SOL_TCP) {
            DescriptorType t = descriptor_getType(descriptor);
            switch (optname) {
                case TCP_INFO: {
                    if(t == DT_TCPSOCKET) {
                        if(optval) {
                            TCP* tcp = (TCP*)descriptor;
                            tcp_getInfo(tcp, (struct tcp_info *)optval);
                        }
                        *optlen = sizeof(struct tcp_info);
                        result = 0;
                    } else {
                        warning("called getsockopt with TCP_INFO on non-TCP socket");
                        errno = ENOPROTOOPT;
                        result = -1;
                    }

                    break;
                }

                case SO_SNDBUF: {
                    if(*optlen < sizeof(gint)) {
                        warning("called getsockopt with SO_SNDBUF with optlen < %i", (gint)(sizeof(gint)));
                        errno = EINVAL;
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called getsockopt with SO_SNDBUF on non-socket");
                        errno = ENOPROTOOPT;
                        result = -1;
                    } else {
                        if(optval) {
                            *((gint*) optval) = (gint) socket_getOutputBufferSize((Socket*)descriptor);
                        }
                        *optlen = sizeof(gint);
                    }
                    break;
                }

                case SO_RCVBUF: {
                    if(*optlen < sizeof(gint)) {
                        warning("called getsockopt with SO_RCVBUF with optlen < %i", (gint)(sizeof(gint)));
                        errno = EINVAL;
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called getsockopt with SO_RCVBUF on non-socket");
                        errno = ENOPROTOOPT;
                        result = -1;
                    } else {
                        if(optval) {
                            *((gint*) optval) = (gint) socket_getInputBufferSize((Socket*)descriptor);
                        }
                        *optlen = sizeof(gint);
                    }
                    break;
                }

                case SO_ERROR: {
                    if(optval) {
                        *((gint*)optval) = 0;
                    }
                    *optlen = sizeof(gint);

                    result = 0;
                    break;
                }

                default: {
                    warning("getsockopt optname %i not implemented", optname);
                    errno = ENOSYS;
                    result = -1;
                    break;
                }
            }
        } else {
            warning("getsockopt level %i not implemented", level);
            errno = ENOSYS;
            result = -1;
        }
    } else {
        errno = EBADF;
        result = -1;
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

int setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", setsockopt);
        return director.libc.setsockopt(fd, level, optname, optval, optlen);
    }

    if(!optval) {
        errno = EFAULT;
        return -1;
    }

    Host* node = _interposer_switchInShadowContext();
    Descriptor* descriptor = host_lookupDescriptor(node, fd);

    gint result = 0;

    /* TODO: implement socket options */
    if(descriptor) {
        if(level == SOL_SOCKET) {
            DescriptorType t = descriptor_getType(descriptor);
            switch (optname) {
                case SO_SNDBUF: {
                    if(optlen < sizeof(gint)) {
                        warning("called setsockopt with SO_SNDBUF with optlen < %i", (gint)(sizeof(gint)));
                        errno = EINVAL;
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called setsockopt with SO_SNDBUF on non-socket");
                        errno = ENOPROTOOPT;
                        result = -1;
                    } else {
                        gint v = *((gint*) optval);
                        socket_setOutputBufferSize((Socket*)descriptor, (gsize)v*2);
                    }
                    break;
                }

                case SO_RCVBUF: {
                    if(optlen < sizeof(gint)) {
                        warning("called setsockopt with SO_RCVBUF with optlen < %i", (gint)(sizeof(gint)));
                        errno = EINVAL;
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called setsockopt with SO_RCVBUF on non-socket");
                        errno = ENOPROTOOPT;
                        result = -1;
                    } else {
                        gint v = *((gint*) optval);
                        socket_setInputBufferSize((Socket*)descriptor, (gsize)v*2);
                    }
                    break;
                }

                case SO_REUSEADDR: {
                    // TODO implement this!
                    // XXX Tor actually uses this option!!
                    debug("setsockopt SO_REUSEADDR not yet implemented");
                    break;
                }

                default: {
                    warning("setsockopt optname %i not implemented", optname);
                    errno = ENOSYS;
                    result = -1;
                    break;
                }
            }
        } else {
            warning("setsockopt level %i not implemented", level);
            errno = ENOSYS;
            result = -1;
        }
    } else {
        errno = EBADF;
        result = -1;
    }

    _interposer_switchOutShadowContext(node);
    return result;
}

int listen(int fd, int n) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", listen);
        return director.libc.listen(fd, n);
    }

    /* check if this is a socket */
    Host* node = _interposer_switchInShadowContext();
    if(!host_isShadowDescriptor(node, fd)){
        _interposer_switchOutShadowContext(node);
        errno = EBADF;
        return -1;
    }

    gint result = host_listenForPeer(node, fd, n);
    _interposer_switchOutShadowContext(node);

    /* check if there was an error */
    if(result != 0) {
        errno = result;
        return -1;
    }

    return 0;
}

int accept(int fd, struct sockaddr* addr, socklen_t* addr_len)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", accept);
        return director.libc.accept(fd, addr, addr_len);
    }

    Host* node = _interposer_switchInShadowContext();
    gint result = 0;

    /* check if this is a virtual socket */
    if(!host_isShadowDescriptor(node, fd)){
        warning("intercepted a non-virtual descriptor");
        result = EBADF;
    }

    in_addr_t ip = 0;
    in_port_t port = 0;
    gint handle = 0;

    if(result == 0) {
        /* direct to node for further checks */
        result = host_acceptNewPeer(node, fd, &ip, &port, &handle);
    }

    _interposer_switchOutShadowContext(node);

    /* check if there was an error */
    if(result != 0) {
        errno = result;
        return -1;
    }

    if(addr != NULL && addr_len != NULL && *addr_len >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in* ai = (struct sockaddr_in*) addr;
        ai->sin_addr.s_addr = ip;
        ai->sin_port = port;
        ai->sin_family = AF_INET;
        *addr_len = sizeof(struct sockaddr_in);
    }

    return handle;
}

int accept4(int fd, struct sockaddr* addr, socklen_t* addr_len, int flags)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", accept4);
        return director.libc.accept4(fd, addr, addr_len, flags);
    }

    /* just ignore the flags and call accept */
    if(flags) {
        Host* node = _interposer_switchInShadowContext();
        debug("accept4 ignoring flags argument");
        _interposer_switchOutShadowContext(node);
    }
    return accept(fd, addr, addr_len);
}

int shutdown(int fd, int how) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", shutdown);
        return director.libc.shutdown(fd, how);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("shutdown not implemented");
    _interposer_switchOutShadowContext(node);
    errno = ENOSYS;
    return -1;
}

ssize_t read(int fd, void *buff, size_t numbytes) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", read);
        return director.libc.read(fd, buff, numbytes);
    }

    gssize ret = 0;
    Host* host = _interposer_switchInShadowContext();

    if(host_isShadowDescriptor(host, fd)){
        Descriptor* desc = host_lookupDescriptor(host, fd);
        if(descriptor_getType(desc) == DT_TIMER) {
            ret = timer_read((Timer*) desc, buff, numbytes);
        } else {
            ret = _interposer_recvHelper(host, fd, buff, numbytes, 0, NULL, 0);
        }
    } else if(host_isRandomHandle(host, fd)) {
        Random* random = host_getRandom(host);
        random_nextNBytes(random, (guchar*)buff, numbytes);
        ret = (ssize_t) numbytes;
    } else {
        gint osfd = host_getOSHandle(host, fd);
        if(osfd >= 0) {
            ret = read(osfd, buff, numbytes);
        } else {
            errno = EBADF;
            ret = -1;
        }
    }

    _interposer_switchOutShadowContext(host);
    return ret;
}

ssize_t write(int fd, const void *buff, size_t n) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", write);
        return director.libc.write(fd, buff, n);
    }

    gssize ret = 0;
    Host* host = _interposer_switchInShadowContext();

    if(host_isShadowDescriptor(host, fd)){
        ret = _interposer_sendHelper(host, fd, buff, n, 0, NULL, 0);
    } else {
        gint osfd = host_getOSHandle(host, fd);
        if(osfd >= 0) {
            ret = write(osfd, buff, n);
        } else {
            errno = EBADF;
            ret = -1;
        }
    }

    _interposer_switchOutShadowContext(host);
    return ret;
}

ssize_t readv(int fd, const struct iovec *iov, int iovcnt) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", readv);
        return director.libc.readv(fd, iov, iovcnt);
    }

    if(iovcnt < 0 || iovcnt > IOV_MAX) {
        errno = EINVAL;
        return -1;
    }

    /* figure out how much they want to read total */
    int i = 0;
    size_t totalIOLength = 0;
    for(i = 0; i < iovcnt; i++) {
        totalIOLength += iov[i].iov_len;
    }

    if(totalIOLength == 0) {
        return 0;
    }

    /* get a temporary buffer and read to it */
    void* tempBuffer = g_malloc0(totalIOLength);
    ssize_t totalBytesRead = read(fd, tempBuffer, totalIOLength);

    if(totalBytesRead > 0) {
        /* place all of the bytes we read in the iov buffers */
        size_t bytesCopied = 0;
        for(i = 0; i < iovcnt; i++) {
            size_t bytesRemaining = (size_t) (totalBytesRead - bytesCopied);
            size_t bytesToCopy = MIN(bytesRemaining, iov[i].iov_len);
            g_memmove(iov[i].iov_base, &tempBuffer[bytesCopied], bytesToCopy);
            bytesCopied += bytesToCopy;
        }
    }

    g_free(tempBuffer);
    return totalBytesRead;
}

ssize_t writev(int fd, const struct iovec *iov, int iovcnt) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", writev);
        return director.libc.writev(fd, iov, iovcnt);
    }

    if(iovcnt < 0 || iovcnt > IOV_MAX) {
        errno = EINVAL;
        return -1;
    }

    /* figure out how much they want to write total */
    int i = 0;
    size_t totalIOLength = 0;
    for(i = 0; i < iovcnt; i++) {
        totalIOLength += iov[i].iov_len;
    }

    if(totalIOLength == 0) {
        return 0;
    }

    /* get a temporary buffer and write to it */
    void* tempBuffer = g_malloc0(totalIOLength);
    size_t bytesCopied = 0;
    for(i = 0; i < iovcnt; i++) {
        g_memmove(&tempBuffer[bytesCopied], iov[i].iov_base, iov[i].iov_len);
        bytesCopied += iov[i].iov_len;
    }

    ssize_t totalBytesWritten = 0;
    if(bytesCopied > 0) {
        /* try to write all of the bytes we got from the iov buffers */
        totalBytesWritten = write(fd, tempBuffer, bytesCopied);
    }

    g_free(tempBuffer);
    return totalBytesWritten;
}

#include <assert.h>
ssize_t pread(int fd, void *buff, size_t numbytes, off_t offset) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", pread);
        return director.libc.pread(fd, buff, numbytes, offset);
    }

    gssize ret = 0;
    Host* host = _interposer_switchInShadowContext();

    if(host_isShadowDescriptor(host, fd)){
    assert(0);
    errno = EBADF;
    ret = -1;
    } else {
        gint osfd = host_getOSHandle(host, fd);
        if(osfd >= 0) {
            ret = pread(osfd, buff, numbytes, offset);
        } else {
            errno = EBADF;
            ret = -1;
        }
    }

    _interposer_switchOutShadowContext(host);
    return ret;
}

int close(int fd) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", close);
        return director.libc.close(fd);
    }

    /* check if this is a socket */
    Host* node = _interposer_switchInShadowContext();

    if(!host_isShadowDescriptor(node, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = close(osfd);
            host_destroyShadowHandle(node, fd);
        } else {
            errno = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        return ret;
    }

    gint r = host_closeUser(node, fd);
    _interposer_switchOutShadowContext(node);
    return r;
}

int fcntl(int fd, int cmd, ...) {
    va_list farg;
    va_start(farg, cmd);
    int result = 0;
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", fcntl);
        result = director.libc.fcntl(fd, cmd, va_arg(farg, void*));
    } else {
        result = _interposer_fcntl(fd, cmd, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}

int ioctl(int fd, unsigned long int request, ...) {
    va_list farg;
    va_start(farg, request);
    int result = 0;
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", ioctl);
        result = director.libc.ioctl(fd, request, va_arg(farg, void*));
    } else {
        result = _interposer_ioctl(fd, request, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}

int pipe2(int pipefds[2], int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", pipe2);
        return director.libc.pipe2(pipefds, flags);
    }

    /* we only support non-blocking sockets, and require
     * SOCK_NONBLOCK to be set immediately */
    gboolean isBlocking = TRUE;

    /* clear non-blocking flags if set to get true type */
    if(flags & O_NONBLOCK) {
        flags = flags & ~O_NONBLOCK;
        isBlocking = FALSE;
    }
    if(flags & O_CLOEXEC) {
        flags = flags & ~O_CLOEXEC;
        isBlocking = FALSE;
    }

    Host* node = _interposer_switchInShadowContext();
    gint result = 0;

    /* check inputs for what we support */
    if(isBlocking) {
        warning("we only support non-blocking pipes: please bitwise OR 'O_NONBLOCK' with flags");
        result = EINVAL;
    } else {
        gint handle = host_createDescriptor(node, DT_PIPE);

        Channel* channel = (Channel*) host_lookupDescriptor(node, handle);
        gint linkedHandle = channel_getLinkedHandle(channel);

        pipefds[0] = handle; /* reader */
        pipefds[1] = linkedHandle; /* writer */
    }

    _interposer_switchOutShadowContext(node);

    if(result != 0) {
        errno = result;
        return -1;
    }

    return 0;
}

int pipe(int pipefds[2]) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", pipe);
        return director.libc.pipe(pipefds);
    }

    return pipe2(pipefds, O_NONBLOCK);
}

int eventfd(unsigned int initval, int flags) {
    int result = 0;
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", eventfd);
        result = director.libc.eventfd(initval, flags);
    } else {
        Host* host = _interposer_switchInShadowContext();

        gint osfd = eventfd(initval, flags);
        gint shadowfd = osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

        _interposer_switchOutShadowContext(host);
        result = shadowfd;
    }
    return result;
}

int timerfd_create(int clockid, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", timerfd_create);
        return director.libc.timerfd_create(clockid, flags);
    }

    Host* host = _interposer_switchInShadowContext();

    gint result = host_createDescriptor(host, DT_TIMER);
    if(result > 0) {
        Descriptor* desc = host_lookupDescriptor(host, result);
        if(desc) {
            descriptor_setFlags(desc, flags);
        }
    }

    _interposer_switchOutShadowContext(host);

    return result;
}

int timerfd_settime(int fd, int flags,
                           const struct itimerspec *new_value,
                           struct itimerspec *old_value) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", timerfd_settime);
        return director.libc.timerfd_settime(fd, flags, new_value, old_value);
    }

    gint ret = 0;

    Host* host = _interposer_switchInShadowContext();

    Descriptor* desc = host_lookupDescriptor(host, fd);
    if(!desc) {
        errno = EBADF;
        ret = -1;
    } else if(descriptor_getType(desc) != DT_TIMER) {
        errno = EINVAL;
        ret = -1;
    } else {
        ret = timer_setTime((Timer*)desc, flags, new_value, old_value);
    }

    _interposer_switchOutShadowContext(host);
    return ret;
}

int timerfd_gettime(int fd, struct itimerspec *curr_value) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", timerfd_gettime);
        return director.libc.timerfd_gettime(fd, curr_value);
    }

    gint ret = 0;

    Host* host = _interposer_switchInShadowContext();

    Descriptor* desc = host_lookupDescriptor(host, fd);
    if(!desc) {
        errno = EBADF;
        ret = -1;
    } else if(descriptor_getType(desc) != DT_TIMER) {
        errno = EINVAL;
        ret = -1;
    } else {
        ret = timer_getTime((Timer*)desc, curr_value);
    }

    _interposer_switchOutShadowContext(host);
    return ret;
}


/* file specific */

int fileno(FILE *stream) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", fileno);
        return director.libc.fileno(stream);
    }

    Host* host = _interposer_switchInShadowContext();

    gint osfd = fileno(stream);
    gint shadowfd = host_getShadowHandle(host, osfd);

    _interposer_switchOutShadowContext(host);
    return shadowfd;
}

int open(const char *pathname, int flags, ...) {
    va_list farg;
    va_start(farg, flags);

    int result = 0;

    if(shouldForwardToLibC()) {
        ENSURE(libc, "", open);
        result = director.libc.open(pathname, flags, va_arg(farg, mode_t));
    } else {
        Host* host = _interposer_switchInShadowContext();

        gint osfd = open(pathname, flags, va_arg(farg, mode_t));
        gint shadowfd = osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

        if(utility_isRandomPath((gchar*)pathname)) {
            host_setRandomHandle(host, shadowfd);
        }

        _interposer_switchOutShadowContext(host);
        result = shadowfd;
    }

    va_end(farg);
    return result;
}

int open64(const char *pathname, int flags, ...) {
    va_list farg;
    va_start(farg, flags);

    int result = 0;

    if(shouldForwardToLibC()) {
        ENSURE(libc, "", open64);
        result = director.libc.open64(pathname, flags, va_arg(farg, mode_t));
    } else {
        result = open(pathname, flags, va_arg(farg, mode_t));
    }

    va_end(farg);
    return result;
}

int creat(const char *pathname, mode_t mode) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", creat);
        return director.libc.creat(pathname, mode);
    }

    Host* host = _interposer_switchInShadowContext();

    gint osfd = creat(pathname, mode);
    gint shadowfd = osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

    _interposer_switchOutShadowContext(host);
    return shadowfd;
}

FILE *fopen(const char *path, const char *mode) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", fopen);
        return director.libc.fopen(path, mode);
    }

    Host* host = _interposer_switchInShadowContext();

    FILE* osfile = fopen(path, mode);
    if(osfile) {
        gint osfd = fileno(osfile);
        gint shadowfd = osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

        if(utility_isRandomPath((gchar*)path)) {
            host_setRandomHandle(host, shadowfd);
        }
    }

    _interposer_switchOutShadowContext(host);
    return osfile;
}

FILE *fdopen(int fd, const char *mode) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", fdopen);
        return director.libc.fdopen(fd, mode);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fdopen not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            FILE* osfile = fdopen(osfd, mode);
            _interposer_switchOutShadowContext(host);
            return osfile;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return NULL;
}

int dup(int oldfd) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", dup);
        return director.libc.dup(oldfd);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, oldfd)) {
        warning("dup not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfdOld = host_getOSHandle(host, oldfd);
        if (osfdOld >= 0) {
            gint osfd = dup(osfdOld);
            gint shadowfd = osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;
            _interposer_switchOutShadowContext(host);
            return osfd;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int dup2(int oldfd, int newfd) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", dup2);
        return director.libc.dup2(oldfd, newfd);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, oldfd) || host_isShadowDescriptor(host, newfd)) {
        warning("dup2 not implemented for Shadow descriptor types");
    } else {
        /* check if we have mapped os fds */
        gint osfdOld = host_getOSHandle(host, oldfd);
        gint osfdNew = host_getOSHandle(host, newfd);

        /* if the newfd is not mapped, then we need to map it later */
        gboolean isMapped = osfdNew >= 3 ? TRUE : FALSE;
        osfdNew = osfdNew == -1 ? newfd : osfdNew;

        if (osfdOld >= 0) {
            gint osfd = dup2(osfdOld, osfdNew);

            gint shadowfd = !isMapped && osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

            _interposer_switchOutShadowContext(host);
            return shadowfd;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int dup3(int oldfd, int newfd, int flags) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", dup3);
        return director.libc.dup3(oldfd, newfd, flags);
    }

    if(oldfd == newfd) {
        errno = EINVAL;
        return -1;
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, oldfd) || host_isShadowDescriptor(host, newfd)) {
        warning("dup3 not implemented for Shadow descriptor types");
    } else {
        /* check if we have mapped os fds */
        gint osfdOld = host_getOSHandle(host, oldfd);
        gint osfdNew = host_getOSHandle(host, newfd);

        /* if the newfd is not mapped, then we need to map it later */
        gboolean isMapped = osfdNew >= 3 ? TRUE : FALSE;
        osfdNew = osfdNew == -1 ? newfd : osfdNew;

        if (osfdOld >= 0) {
            gint osfd = dup3(osfdOld, osfdNew, flags);

            gint shadowfd = !isMapped && osfd >= 3 ? host_createShadowHandle(host, osfd) : osfd;

            _interposer_switchOutShadowContext(host);
            return shadowfd;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int fclose(FILE *fp) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", fclose);
        return director.libc.fclose(fp);
    }

    Host* host = _interposer_switchInShadowContext();

    gint osfd = fileno(fp);
    gint shadowHandle = host_getShadowHandle(host, osfd);

    gint ret = fclose(fp);
    host_destroyShadowHandle(host, shadowHandle);

    _interposer_switchOutShadowContext(host);
    return ret;
}

/* fstat redirects to this */
int __fxstat (int ver, int fd, struct stat *buf) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", __fxstat);
        return director.libc.__fxstat(ver, fd, buf);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fstat not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = fstat(osfd, buf);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

/* fstat64 redirects to this */
int __fxstat64 (int ver, int fd, struct stat64 *buf) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", __fxstat64);
        return director.libc.__fxstat64(ver, fd, buf);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fstat64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = fstat64(osfd, buf);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int fstatfs (int fd, struct statfs *buf) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", fstatfs);
        return director.libc.fstatfs(fd, buf);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fstatfs not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = fstatfs(osfd, buf);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int fstatfs64 (int fd, struct statfs64 *buf) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", fstatfs64);
        return director.libc.fstatfs64(fd, buf);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fstatfs64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = fstatfs64(osfd, buf);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

off_t lseek(int fd, off_t offset, int whence) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", lseek);
        return director.libc.lseek(fd, offset, whence);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("lseek not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            off_t ret = lseek(osfd, offset, whence);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return (off_t)-1;
}

int flock(int fd, int operation) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", flock);
        return director.libc.flock(fd, operation);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("flock not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = flock(osfd, operation);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return (off_t)-1;
}

int fsync(int fd) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", fsync);
        return director.libc.fsync(fd);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("fsync not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = fsync(osfd);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int ftruncate(int fd, off_t length) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", ftruncate);
        return director.libc.ftruncate(fd, length);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("ftruncate not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = ftruncate(osfd, length);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

int posix_fallocate(int fd, off_t offset, off_t len) {
    if (shouldForwardToLibC()) {
        ENSURE(libc, "", posix_fallocate);
        return director.libc.posix_fallocate(fd, offset, len);
    }

    Host* host = _interposer_switchInShadowContext();

    if (host_isShadowDescriptor(host, fd)) {
        warning("posix_fallocate not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(host, fd);
        if (osfd >= 0) {
            gint ret = posix_fallocate(osfd, offset, len);
            _interposer_switchOutShadowContext(host);
            return ret;
        }
    }

    _interposer_switchOutShadowContext(host);

    errno = EBADF;
    return -1;
}

//TODO
//int fstatvfs(int fd, struct statvfs *buf);
//
//int fdatasync(int fd);
//
//int syncfs(int fd);
//
//int fallocate(int fd, int mode, off_t offset, off_t len);
//
//int fexecve(int fd, char *const argv[], char *const envp[]);
//
//long fpathconf(int fd, int name);
//
//int fchdir(int fd);
//
//int fchown(int fd, uid_t owner, gid_t group);
//
//int fchmod(int fd, mode_t mode);
//
//int posix_fadvise(int fd, off_t offset, off_t len, int advice);
//
//int lockf(int fd, int cmd, off_t len);
//
//int openat(int dirfd, const char *pathname, int flags, mode_t mode);
//
//int faccessat(int dirfd, const char *pathname, int mode, int flags);
//
//int unlinkat(int dirfd, const char *pathname, int flags);
//
//int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
//
//int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);

/* time family */

time_t time(time_t *t)  {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", time);
        return director.libc.time(t);
    }

    Host* node = _interposer_switchInShadowContext();
    time_t secs = (time_t) (worker_getCurrentTime() / SIMTIME_ONE_SECOND);
    if(t != NULL){
        *t = secs;
    }
    _interposer_switchOutShadowContext(node);
    return secs;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", clock_gettime);
        return director.libc.clock_gettime(clk_id, tp);
    }

    if(tp == NULL) {
        errno = EFAULT;
        return -1;
    }

    Host* node = _interposer_switchInShadowContext();

    SimulationTime now = worker_getCurrentTime();
    tp->tv_sec = now / SIMTIME_ONE_SECOND;
    tp->tv_nsec = now % SIMTIME_ONE_SECOND;

    _interposer_switchOutShadowContext(node);
    return 0;
}

int gettimeofday(struct timeval* tv, __timezone_ptr_t tz) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gettimeofday);
        return director.libc.gettimeofday(tv, tz);
    }

    if(tv) {
        Host* node = _interposer_switchInShadowContext();
        SimulationTime now = worker_getCurrentTime();
        tv->tv_sec = now / SIMTIME_ONE_SECOND;
        tv->tv_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;
        _interposer_switchOutShadowContext(node);
    }
    return 0;
}


/* name/address family */


int gethostname(char* name, size_t len) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostname);
        return director.libc.gethostname(name, len);
    }

    Host* node = _interposer_switchInShadowContext();
    gint result = 0;

//  in_addr_t ip = node_getDefaultIP(node);
//  const gchar* hostname = internetwork_resolveID(worker_getPrivate()->cached_engine->internet, (GQuark)ip);

    if(name != NULL && node != NULL) {
        /* resolve my address to a hostname */
        const gchar* sysname = host_getName(node);

        if(sysname != NULL && len > strlen(sysname)) {
            if(strncpy(name, sysname, len) != NULL) {
                _interposer_switchOutShadowContext(node);
                return 0;
            }
        }
    }

    errno = EFAULT;
    return -1;
}

int getaddrinfo(const char *node, const char *service,
const struct addrinfo *hints, struct addrinfo **res) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getaddrinfo);
        return director.libc.getaddrinfo(node, service, hints, res);
    }

    if(node == NULL && service == NULL) {
        errno = EINVAL;
        return EAI_NONAME;
    }

    Host* host = _interposer_switchInShadowContext();

    gint result = 0;
    *res = NULL;

    in_addr_t ip = INADDR_NONE;
    in_port_t port = 0;

    if(node == NULL) {
        if(hints && (hints->ai_flags & AI_PASSIVE)) {
            ip = htonl(INADDR_ANY);
        } else {
            ip = htonl(INADDR_LOOPBACK);
        }
    } else {
        Address* address = NULL;

        /* node may be a number-and-dots address, or a hostname. lets find out which. */
        ip = address_stringToIP(node);

        if(ip == INADDR_NONE) {
            /* if AI_NUMERICHOST, don't do hostname lookup */
            if(!hints || (hints && !(hints->ai_flags & AI_NUMERICHOST))) {
                /* the node string is not a dots-and-decimals string, try it as a hostname */
                address = dns_resolveNameToAddress(worker_getDNS(), node);
            }
        } else {
            /* we got an ip from the string, so lookup by the ip */
            address = dns_resolveIPToAddress(worker_getDNS(), ip);
        }

        if(address) {
            /* found it */
            ip = address_toNetworkIP(address);
        } else {
            /* at this point it is an error */
            ip = INADDR_NONE;
            errno = EINVAL;
            result = EAI_NONAME;
        }
    }

    if(service) {
        /* get the service name if possible */
        if(!hints || (hints && !(hints->ai_flags & AI_NUMERICSERV))) {
            /* XXX this is not thread safe! */
            struct servent* serviceEntry = getservbyname(service, NULL);
            if(serviceEntry) {
                port = (in_port_t) serviceEntry->s_port;
            }
        }

        /* if not found, try converting string directly to port */
        if(port == 0) {
            port = (in_port_t)strtol(service, NULL, 10);
        }
    }

    if(ip != INADDR_NONE) {
        /* should have address now */
        struct sockaddr_in* sa = g_malloc(sizeof(struct sockaddr_in));
        /* application will expect it in network order */
        // sa->sin_addr.s_addr = (in_addr_t) htonl((guint32)(*addr));
        sa->sin_addr.s_addr = ip;
        sa->sin_family = AF_INET; /* libcurl expects this to be set */
        sa->sin_port = port;

        struct addrinfo* ai_out = g_malloc(sizeof(struct addrinfo));
        ai_out->ai_addr = (struct sockaddr*) sa;
        ai_out->ai_addrlen =  sizeof(struct sockaddr_in);
        ai_out->ai_canonname = NULL;
        ai_out->ai_family = AF_INET;
        ai_out->ai_flags = 0;
        ai_out->ai_next = NULL;
        ai_out->ai_protocol = 0;
        ai_out->ai_socktype = SOCK_STREAM;

        *res = ai_out;
        result = 0;
    }

    _interposer_switchOutShadowContext(host);
    return result;
}

void freeaddrinfo(struct addrinfo *res) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", freeaddrinfo);
        director.libc.freeaddrinfo(res);
        return;
    }

    Host* node = _interposer_switchInShadowContext();
    if(res && res->ai_addr != NULL) {
        g_free(res->ai_addr);
        res->ai_addr = NULL;
        g_free(res);
    }
    _interposer_switchOutShadowContext(node);
}

int getnameinfo(const struct sockaddr* sa, socklen_t salen,
        char * host, socklen_t hostlen, char *serv, socklen_t servlen,
        /* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
        int flags) {
#else
        unsigned int flags) {
#endif

    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getnameinfo);
        return director.libc.getnameinfo(sa, salen, host, hostlen, serv, servlen, (int)flags);
    }

    /* FIXME this is not fully implemented */
    if(!sa) {
        return EAI_FAIL;
    }

    gint retval = 0;
    Host* node = _interposer_switchInShadowContext();

    GQuark convertedIP = (GQuark) (((struct sockaddr_in*)sa)->sin_addr.s_addr);
    const gchar* hostname = dns_resolveIPToName(worker_getDNS(), convertedIP);

    if(hostname) {
        g_utf8_strncpy(host, hostname, hostlen);
    } else {
        retval = EAI_NONAME;
    }

    _interposer_switchOutShadowContext(node);
    return retval;
}

struct hostent* gethostbyname(const gchar* name) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyname);
        return director.libc.gethostbyname(name);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyname not yet implemented");
    _interposer_switchOutShadowContext(node);
    return NULL;
}

int gethostbyname_r(const gchar *name, struct hostent *ret, gchar *buf,
gsize buflen, struct hostent **result, gint *h_errnop) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyname_r);
        return director.libc.gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyname_r not yet implemented");
    _interposer_switchOutShadowContext(node);
    return -1;
}

struct hostent* gethostbyname2(const gchar* name, gint af) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyname2);
        return director.libc.gethostbyname2(name, af);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyname2 not yet implemented");
    _interposer_switchOutShadowContext(node);
    return NULL;
}

int gethostbyname2_r(const gchar *name, gint af, struct hostent *ret,
        gchar *buf, gsize buflen, struct hostent **result, gint *h_errnop) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyname2_r);
        return director.libc.gethostbyname2_r(name, af, ret, buf, buflen, result, h_errnop);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyname2_r not yet implemented");
    _interposer_switchOutShadowContext(node);
    return -1;
}

struct hostent* gethostbyaddr(const void* addr, socklen_t len, gint type) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyaddr);
        return director.libc.gethostbyaddr(addr, len, type);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyaddr not yet implemented");
    _interposer_switchOutShadowContext(node);
    return NULL;
}

int gethostbyaddr_r(const void *addr, socklen_t len, gint type,
struct hostent *ret, char *buf, gsize buflen, struct hostent **result,
gint *h_errnop) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", gethostbyaddr_r);
        return director.libc.gethostbyaddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
    }

    Host* node = _interposer_switchInShadowContext();
    warning("gethostbyaddr_r not yet implemented");
    _interposer_switchOutShadowContext(node);
    return -1;
}

/* random family */

int rand() {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", rand);
        return director.libc.rand();
    }

    Host* node = _interposer_switchInShadowContext();
    gint r = random_nextInt(host_getRandom(node));
    _interposer_switchOutShadowContext(node);
    return r;
}

int rand_r(unsigned int *seedp) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", rand_r);
        return director.libc.rand_r(seedp);
    }

    Host* node = _interposer_switchInShadowContext();
    gint r = random_nextInt(host_getRandom(node));
    _interposer_switchOutShadowContext(node);
    return r;
}

void srand(unsigned int seed) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", srand);
        director.libc.srand(seed);
        return;
    }

    return;
}

long int random() {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", random);
        return director.libc.random();
    }

    Host* node = _interposer_switchInShadowContext();
    gint r = random_nextInt(host_getRandom(node));
    _interposer_switchOutShadowContext(node);
    return (long int)r;
}

int random_r(struct random_data *buf, int32_t *result) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", random_r);
        return director.libc.random_r(buf, result);
    }

    Host* node = _interposer_switchInShadowContext();
    utility_assert(result != NULL);
    *result = (int32_t)random_nextInt(host_getRandom(node));
    _interposer_switchOutShadowContext(node);
    return 0;
}

void srandom(unsigned int seed) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", srandom);
        director.libc.srandom(seed);
    }

    return;
}

int srandom_r(unsigned int seed, struct random_data *buf) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", srandom_r);
        return director.libc.srandom_r(seed, buf);
    }

    return 0;
}

/* exit family */

int on_exit(void (*function)(int , void *), void *arg) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", on_exit);
        return director.libc.on_exit(function, arg);
    }

    Host* host = _interposer_switchInShadowContext();

    gboolean success = FALSE;
    Thread* thread = worker_getActiveThread();
    if(thread) {
        Process* proc = thread_getParentProcess(thread);
        success = process_addAtExitCallback(proc, function, arg, TRUE);
    }

    _interposer_switchOutShadowContext(host);

    return success == TRUE ? 0 : -1;
}

int atexit(void (*func)(void)) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", atexit);
        return director.libc.atexit(func);
    }

    Host* host = _interposer_switchInShadowContext();

    gboolean success = FALSE;
    Thread* thread = worker_getActiveThread();
    if(thread) {
        Process* proc = thread_getParentProcess(thread);
        success = process_addAtExitCallback(proc, func, NULL, FALSE);
    }

    _interposer_switchOutShadowContext(host);

    return success == TRUE ? 0 : -1;
}

int __cxa_atexit(void (*func) (void *), void * arg, void * dso_handle) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", __cxa_atexit);
        return director.libc.__cxa_atexit(func, arg, dso_handle);
    }

    Host* host = _interposer_switchInShadowContext();

    gboolean success = FALSE;
    if(dso_handle) {
        /* this should be called when the plugin is unloaded */
        warning("atexit at library close is not currently supported");
    } else {
        Thread* thread = worker_getActiveThread();
        if(thread) {
            Process* proc = thread_getParentProcess(thread);
            success = process_addAtExitCallback(proc, func, arg, TRUE);
        }
    }

    _interposer_switchOutShadowContext(host);

    return success == TRUE ? 0 : -1;
}
