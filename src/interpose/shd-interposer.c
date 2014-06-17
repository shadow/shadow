/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#define _GNU_SOURCE
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
#include <sys/types.h>
#include <sys/file.h>
#include <linux/sockios.h>
#include <features.h>

#include <malloc.h>

#include "shadow.h"

#define SETSYM_OR_FAIL(funcptr, funcstr) { \
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

typedef void* (*MallocFunc)(size_t);
typedef void* (*CallocFunc)(size_t, size_t);
typedef void* (*ReallocFunc)(void*, size_t);
typedef int (*PosixMemalignFunc)(void**, size_t, size_t);
typedef void* (*MemalignFunc)(size_t, size_t);
typedef void* (*AlignedAllocFunc)(size_t, size_t);
typedef void* (*VallocFunc)(size_t);
typedef void* (*PvallocFunc)(size_t);
typedef void (*FreeFunc)(void*);
typedef void* (*MMapFunc)(void *, size_t, int, int, int, off_t);

/* event family */

typedef int (*EpollCreateFunc)(int);
typedef int (*EpollCreate1Func)(int);
typedef int (*EpollCtlFunc)(int, int, int, struct epoll_event*);
typedef int (*EpollWaitFunc)(int, struct epoll_event*, int, int);
typedef int (*EpollPWaitFunc)(int, struct epoll_event*, int, int, const sigset_t*);

/* socket/io family */

typedef int (*SocketFunc)(int, int, int);
typedef int (*SocketpairFunc)(int, int, int, int[]);
typedef int (*BindFunc)(int, __CONST_SOCKADDR_ARG, socklen_t);
typedef int (*GetsocknameFunc)(int, __SOCKADDR_ARG, socklen_t*);
typedef int (*ConnectFunc)(int, __CONST_SOCKADDR_ARG, socklen_t);
typedef int (*GetpeernameFunc)(int, __SOCKADDR_ARG, socklen_t*);
typedef size_t (*SendFunc)(int, const void*, size_t, int);
typedef size_t (*SendtoFunc)(int, const void*, size_t, int, __CONST_SOCKADDR_ARG, socklen_t);
typedef size_t (*SendmsgFunc)(int, const struct msghdr*, int);
typedef size_t (*RecvFunc)(int, void*, size_t, int);
typedef size_t (*RecvfromFunc)(int, void*, size_t, int, __SOCKADDR_ARG, socklen_t*);
typedef size_t (*RecvmsgFunc)(int, struct msghdr*, int);
typedef int (*GetsockoptFunc)(int, int, int, void*, socklen_t*);
typedef int (*SetsockoptFunc)(int, int, int, const void*, socklen_t);
typedef int (*ListenFunc)(int, int);
typedef int (*AcceptFunc)(int, __SOCKADDR_ARG, socklen_t*);
typedef int (*Accept4Func)(int, __SOCKADDR_ARG, socklen_t*, int);
typedef int (*ShutdownFunc)(int, int);
typedef int (*PipeFunc)(int [2]);
typedef int (*Pipe2Func)(int [2], int);
typedef size_t (*ReadFunc)(int, void*, size_t);
typedef size_t (*WriteFunc)(int, const void*, size_t);
typedef int (*CloseFunc)(int);
typedef int (*FcntlFunc)(int, int, ...);
typedef int (*IoctlFunc)(int, int, ...);

/* file specific */

typedef int (*FileNoFunc)(FILE *);
typedef int (*OpenFunc)(const char*, int, mode_t);
typedef int (*Open64Func)(const char*, int, mode_t);
typedef int (*CreatFunc)(const char*, mode_t);
typedef FILE* (*FOpenFunc)(const char *, const char *);
typedef FILE* (*FDOpenFunc)(int, const char*);
typedef int (*FCloseFunc)(FILE *);
typedef int (*DupFunc)(int);
typedef int (*Dup2Func)(int, int);
typedef int (*Dup3Func)(int, int, int);
typedef int (*FXStat)(int, int, struct stat*);
typedef int (*FStatFSFunc)(int, struct statfs*);
typedef off_t (*LSeekFunc)(int, off_t, int);
typedef int (*FLockFunc)(int, int);

/* time family */

typedef time_t (*TimeFunc)(time_t*);
typedef int (*ClockGettimeFunc)(clockid_t, struct timespec *);
typedef int (*GettimeofdayFunc)(struct timeval*, __timezone_ptr_t);

/* name/address family */

typedef int (*GethostnameFunc)(char*, size_t);
typedef int (*GetaddrinfoFunc)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
typedef int (*FreeaddrinfoFunc)(struct addrinfo*);
typedef int (*GetnameinfoFunc)(const struct sockaddr *, socklen_t, char *, size_t, char *, size_t, int);
typedef struct hostent* (*GethostbynameFunc)(const char*);
typedef int (*GethostbynameRFunc)(const char*, struct hostent*, char*, size_t, struct hostent**, int*);
typedef struct hostent* (*Gethostbyname2Func)(const char*, int);
typedef int (*Gethostbyname2RFunc)(const char*, int, struct hostent *, char *, size_t, struct hostent**, int*);
typedef struct hostent* (*GethostbyaddrFunc)(const void*, socklen_t, int);
typedef int (*GethostbyaddrRFunc)(const void*, socklen_t, int, struct hostent*, char*, size_t, struct hostent **, int*);

/* random family */

typedef int (*RandFunc)();
typedef int (*RandRFunc)(unsigned int*);
typedef void (*SrandFunc)(unsigned int);
typedef long int (*RandomFunc)(void);
typedef int (*RandomRFunc)(struct random_data*, int32_t*);
typedef void (*SrandomFunc)(unsigned int);
typedef int (*SrandomRFunc)(unsigned int, struct random_data*);

typedef struct {
	MallocFunc malloc;
	CallocFunc calloc;
	ReallocFunc realloc;
	PosixMemalignFunc posix_memalign;
	MemalignFunc memalign;
	AlignedAllocFunc aligned_alloc;
	VallocFunc valloc;
	PvallocFunc pvalloc;
	FreeFunc free;
	MMapFunc mmap;

	EpollCreateFunc epoll_create;
	EpollCreate1Func epoll_create1;
	EpollCtlFunc epoll_ctl;
	EpollWaitFunc epoll_wait;
	EpollPWaitFunc epoll_pwait;

	SocketFunc socket;
	SocketpairFunc socketpair;
	BindFunc bind;
	GetsocknameFunc getsockname;
	ConnectFunc connect;
	GetpeernameFunc getpeername;
	SendFunc send;
	SendtoFunc sendto;
	SendmsgFunc sendmsg;
	RecvFunc recv;
	RecvfromFunc recvfrom;
	RecvmsgFunc recvmsg;
	GetsockoptFunc getsockopt;
	SetsockoptFunc setsockopt;
	ListenFunc listen;
	AcceptFunc accept;
	Accept4Func accept4;
	ShutdownFunc shutdown;
	PipeFunc pipe;
	Pipe2Func pipe2;
	ReadFunc read;
	WriteFunc write;
	CloseFunc close;
	FcntlFunc fcntl;
	IoctlFunc ioctl;

	FileNoFunc fileno;
	OpenFunc open;
	Open64Func open64;
	CreatFunc creat;
	FOpenFunc fopen;
	FDOpenFunc fdopen;
	DupFunc dup;
	Dup2Func dup2;
	Dup3Func dup3;
	FCloseFunc fclose;
	FXStat __fxstat;
	FStatFSFunc fstatfs;
	LSeekFunc lseek;
	FLockFunc flock;

	TimeFunc time;
	ClockGettimeFunc clock_gettime;
	GettimeofdayFunc gettimeofday;

	GethostnameFunc gethostname;
	GetaddrinfoFunc getaddrinfo;
	FreeaddrinfoFunc freeaddrinfo;
	GetnameinfoFunc getnameinfo;
	GethostbynameFunc gethostbyname;
	GethostbynameRFunc gethostbyname_r;
	Gethostbyname2Func gethostbyname2;
	Gethostbyname2RFunc gethostbyname2_r;
	GethostbyaddrFunc gethostbyaddr;
	GethostbyaddrRFunc gethostbyaddr_r;

	RandFunc rand;
	RandRFunc rand_r;
	SrandFunc srand;
	RandomFunc random;
	RandomRFunc random_r;
	SrandomFunc srandom;
	SrandomRFunc srandom_r;
} PreloadFuncs;

typedef struct {
	struct {
		char buf[102400];
		size_t pos;
		size_t nallocs;
		size_t ndeallocs;
	} dummy;
	PreloadFuncs libc;
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

static void _interposer_globalInitialize() {
	/* ensure we never intercept during initialization */
	__sync_fetch_and_add(&isRecursive, 1);

	memset(&director, 0, sizeof(FuncDirector));

	/* use dummy malloc during initial dlsym calls to avoid recursive stack segfaults */
	director.libc.malloc = dummy_malloc;
	director.libc.calloc = dummy_calloc;
	director.libc.free = dummy_free;

	MallocFunc tempMalloc;
	CallocFunc tempCalloc;
	FreeFunc tempFree;

	SETSYM_OR_FAIL(tempMalloc, "malloc");
	SETSYM_OR_FAIL(tempCalloc, "calloc");
	SETSYM_OR_FAIL(tempFree, "free");

	/* stop using the dummy malloc funcs now */
	director.libc.malloc = tempMalloc;
	director.libc.calloc = tempCalloc;
	director.libc.free = tempFree;

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
	    Thread* thread = worker_isAlive() ? worker_getActiveThread() : NULL;
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
    } else if(len == NULL || *len < sizeof(struct sockaddr_in)) {
        result = EINVAL;
    }

    if(result == 0) {
        struct sockaddr_in* saddr = (struct sockaddr_in*) addr;
        in_addr_t ip = saddr->sin_addr.s_addr;
        in_port_t port = saddr->sin_port;
        sa_family_t family = saddr->sin_family;

        /* direct to node for further checks */

        switch(type) {
            case SCT_BIND: {
                result = host_bindToInterface(host, fd, ip, port);
                break;
            }

            case SCT_CONNECT: {
                result = host_connectToPeer(host, fd, ip, port, family);
                break;
            }

            case SCT_GETPEERNAME:
            case SCT_GETSOCKNAME: {
                result = type == SCT_GETPEERNAME ?
                        host_getPeerName(host, fd, &(saddr->sin_addr.s_addr), &(saddr->sin_port)) :
                        host_getSocketName(host, fd, &(saddr->sin_addr.s_addr), &(saddr->sin_port));

                if(result == 0) {
                    saddr->sin_family = AF_INET;
                    *len = sizeof(struct sockaddr_in);
                }

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
        gint err = 0, ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = fcntl(osfd, cmd, farg);
        } else {
            err = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        errno = err;
        return ret;
    }

    _interposer_switchOutShadowContext(node);

    /* normally, the type of farg depends on the cmd */

    return 0;
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
        gint err = 0, ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = ioctl(fd, request, farg);
        } else {
            err = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        errno = err;
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

    if(result != 0) {
        errno = result;
        return -1;
    } else {
        return 0;
    }
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
#if 0
void* aligned_alloc(size_t alignment, size_t size) {
    if(shouldForwardToLibC()) {
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
#endif

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
    } else if(domain != AF_INET) {
        warning("trying to create socket with domain \"%i\", we only support PF_INET", domain);
        errno = EAFNOSUPPORT;
        result = -1;
    }

    if(result == 0) {
        /* we are all set to create the socket */
        DescriptorType dtype = type == SOCK_STREAM ? DT_TCPSOCKET : DT_UDPSOCKET;
        result = host_createDescriptor(node, dtype);
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
        ret = _interposer_recvHelper(host, fd, buff, numbytes, 0, NULL, 0);
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

int close(int fd) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", close);
        return director.libc.close(fd);
    }

    /* check if this is a socket */
    Host* node = _interposer_switchInShadowContext();

    if(!host_isShadowDescriptor(node, fd)){
        gint err = 0, ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(node, fd);
        if(osfd >= 0) {
            ret = close(osfd);
            host_destroyShadowHandle(node, fd);
        } else {
            err = EBADF;
            ret = -1;
        }
        _interposer_switchOutShadowContext(node);
        errno = err;
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

//TODO
//int fstatvfs(int fd, struct statvfs *buf);
//
//int fsync(int fd);
//
//int fdatasync(int fd);
//
//int syncfs(int fd);
//
//int ftruncate(int fd, off_t length);
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
//int posix_fallocate(int fd, off_t offset, off_t len);
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
                result = 0;
                goto done;
            }
        }
    }
    errno = EFAULT;
    result = -1;

    done:

    _interposer_switchOutShadowContext(node);
    return result;
}

int getaddrinfo(const char *name, const char *service,
const struct addrinfo *hints, struct addrinfo **res) {
    if(shouldForwardToLibC()) {
        ENSURE(libc, "", getaddrinfo);
        return director.libc.getaddrinfo(name, service, hints, res);
    }

    Host* node = _interposer_switchInShadowContext();

    gint result = 0;

    *res = NULL;
    if(name != NULL && node != NULL) {

        /* node may be a number-and-dots address, or a hostname. lets hope for hostname
         * and try that first, o/w convert to the in_addr_t and do a second lookup. */
        in_addr_t address = (in_addr_t) dns_resolveNameToIP(worker_getDNS(), name);

        if(address == 0) {
            /* name was not in hostname format. convert to IP format and try again */
            struct in_addr inaddr;
            gint r = inet_pton(AF_INET, name, &inaddr);

            if(r == 1) {
                /* successful conversion to IP format, now find the real hostname */
                GQuark convertedIP = (GQuark) inaddr.s_addr;
                const gchar* hostname = dns_resolveIPToName(worker_getDNS(), convertedIP);

                if(hostname != NULL) {
                    /* got it, so convertedIP is a valid IP */
                    address = (in_addr_t) convertedIP;
                } else {
                    /* name not mapped by resolver... */
                    result = EAI_FAIL;
                    goto done;
                }
            } else if(r == 0) {
                /* not in correct form... hmmm, too bad i guess */
                result = EAI_NONAME;
                goto done;
            } else {
                /* error occured */
                result = EAI_SYSTEM;
                goto done;
            }
        }

        /* should have address now */
        struct sockaddr_in* sa = g_malloc(sizeof(struct sockaddr_in));
        /* application will expect it in network order */
        // sa->sin_addr.s_addr = (in_addr_t) htonl((guint32)(*addr));
        sa->sin_addr.s_addr = address;
        sa->sin_family = AF_INET; /* libcurl expects this to be set */

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
        goto done;
    }

    errno = EINVAL;
    result = EAI_SYSTEM;

    done:
    _interposer_switchOutShadowContext(node);
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
