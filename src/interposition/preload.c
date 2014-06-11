/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <time.h>
#include <netdb.h>
#include <stdarg.h>
#include <features.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>
#include <sys/mman.h>

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

typedef int (*WorkerIsInShadowContextFunc)();

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

/* openssl family - only used if OpenSSL is linked */

typedef void (*AESEncryptFunc)(const unsigned char*, unsigned char*, const void*);
typedef void (*AESDecryptFunc)(const unsigned char*, unsigned char*, const void*);
typedef void (*AESCtr128EncryptFunc)(const unsigned char*, unsigned char*, const void*);
typedef void (*AESCtr128DecryptFunc)(const unsigned char*, unsigned char*, const void*);
typedef int (*EVPCipherFunc)(void*, unsigned char*, const unsigned char*, unsigned int);
typedef void* (*CRYPTOGetLockingCallbackFunc)();
typedef void* (*CRYPTOGetIdCallbackFunc)();
typedef void (*RANDSeedFunc)(const void*, int);
typedef void (*RANDAddFunc)(const void*, int, double);
typedef int (*RANDPollFunc)();
typedef int (*RANDBytesFunc)(unsigned char*, int);
typedef int (*RANDPseudoBytesFunc)(unsigned char*, int);
typedef void (*RANDCleanupFunc)();
typedef int (*RANDStatusFunc)();
typedef const void * (*RANDGetRandMethodFunc)();

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

	AESEncryptFunc AES_encrypt;
	AESDecryptFunc AES_decrypt;
	AESCtr128EncryptFunc AES_ctr128_encrypt;
	AESCtr128DecryptFunc AES_ctr128_decrypt;
	EVPCipherFunc EVP_Cipher;
	CRYPTOGetLockingCallbackFunc CRYPTO_get_locking_callback;
	CRYPTOGetIdCallbackFunc CRYPTO_get_id_callback;

	RANDSeedFunc RAND_seed;
	RANDAddFunc RAND_add;
	RANDPollFunc RAND_poll;
	RANDBytesFunc RAND_bytes;
	RANDPseudoBytesFunc RAND_pseudo_bytes;
	RANDCleanupFunc RAND_cleanup;
	RANDStatusFunc RAND_status;
	RANDGetRandMethodFunc RAND_get_rand_method;
} PreloadFuncs;

typedef struct {
	struct {
		char buf[102400];
		size_t pos;
		size_t nallocs;
		size_t ndeallocs;
	} dummy;
	PreloadFuncs real;
	PreloadFuncs shadow;
	WorkerIsInShadowContextFunc isShadowFunc;
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

static void initialize() {
	/* ensure we never intercept during initialization */
	__sync_fetch_and_add(&isRecursive, 1);

	memset(&director, 0, sizeof(FuncDirector));

	/* use dummy malloc during initial dlsym calls to avoid recursive stack segfaults */
	director.real.malloc = dummy_malloc;
	director.real.calloc = dummy_calloc;
	director.real.free = dummy_free;

	MallocFunc tempMalloc;
	CallocFunc tempCalloc;
	FreeFunc tempFree;

	SETSYM_OR_FAIL(tempMalloc, "malloc");
	SETSYM_OR_FAIL(tempCalloc, "calloc");
	SETSYM_OR_FAIL(tempFree, "free");

	/* stop using the dummy malloc funcs now */
	director.real.malloc = tempMalloc;
	director.real.calloc = tempCalloc;
	director.real.free = tempFree;

    __sync_fetch_and_sub(&isRecursive, 1);
}

static inline int shouldRedirect() {
	int doRedirect = 0;
	/* recursive calls always go to the syscall */
	if(!__sync_fetch_and_add(&isRecursive, 1)) {
		/* check if the shadow intercept library is loaded yet, but dont fail if its not */
		if(!(director.isShadowFunc)) {
			director.isShadowFunc = dlsym(RTLD_NEXT, "intercept_worker_isInShadowContext");
		}
		if(director.isShadowFunc) {
			/* ask shadow if this call is a plug-in that should be intercepted */
			doRedirect = director.isShadowFunc() ? 0 : 1;
		} else {
			/* intercept library is not yet loaded, don't redirect */
			doRedirect = 0;
		}
	}
	__sync_fetch_and_sub(&isRecursive, 1);
	return doRedirect;
}

/* this function is called when the library is loaded,
 * and only once per program not once per thread */
void __attribute__((constructor)) construct() {
	/* here we are guaranteed no threads have started yet */
	initialize();
}

/* this function is called when the library is unloaded */
//void __attribute__((destructor)) destruct() {}

/* memory allocation family */

void* malloc(size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", malloc);
        return director.shadow.malloc(size);
    } else {
        ENSURE(real, "", malloc);
        return director.real.malloc(size);
    }
}

void* calloc(size_t nmemb, size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", calloc);
        return director.shadow.calloc(nmemb, size);
    } else {
    	/* the dlsym lookup for calloc may call calloc again, causing infinite recursion */
    	if(!director.real.calloc) {
    		/* make sure to use dummy_calloc when looking up calloc */
			director.real.calloc = dummy_calloc;
			/* this will set director.real.calloc to the correct calloc */
			ENSURE(real, "", calloc);
    	}
        return director.real.calloc(nmemb, size);
    }
}

void* realloc(void *ptr, size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", realloc);
        return director.shadow.realloc(ptr, size);
    } else {
        ENSURE(real, "", realloc);
        return director.real.realloc(ptr, size);
    }
}

void free(void *ptr) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", free);
        return director.shadow.free(ptr);
    } else {
        ENSURE(real, "", free);
        return director.real.free(ptr);
    }
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", posix_memalign);
        return director.shadow.posix_memalign(memptr, alignment, size);
    } else {
        ENSURE(real, "", posix_memalign);
        return director.real.posix_memalign(memptr, alignment, size);
    }
}

void* memalign(size_t blocksize, size_t bytes) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", memalign);
        return director.shadow.memalign(blocksize, bytes);
    } else {
        ENSURE(real, "", memalign);
        return director.real.memalign(blocksize, bytes);
    }
}

/* aligned_alloc doesnt exist in glibc in the current LTS version of ubuntu */
#if 0
void* aligned_alloc(size_t alignment, size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", aligned_alloc);
        return director.shadow.aligned_alloc(alignment, size);
    } else {
        ENSURE(real, "", aligned_alloc);
        return director.real.aligned_alloc(alignment, size);
    }
}
#endif

void* valloc(size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", valloc);
        return director.shadow.valloc(size);
    } else {
        ENSURE(real, "", valloc);
        return director.real.valloc(size);
    }
}

void* pvalloc(size_t size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", pvalloc);
        return director.shadow.pvalloc(size);
    } else {
        ENSURE(real, "", pvalloc);
        return director.real.pvalloc(size);
    }
}

/* for fd translation */
void* mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", mmap);
        return director.shadow.mmap(addr, length, prot, flags, fd, offset);
    } else {
        ENSURE(real, "", mmap);
        return director.real.mmap(addr, length, prot, flags, fd, offset);
    }
}

/* event family */

int epoll_create(int size) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", epoll_create);
        return director.shadow.epoll_create(size);
    } else {
        ENSURE(real, "", epoll_create);
        return director.real.epoll_create(size);
    }
}

int epoll_create1(int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", epoll_create1);
        return director.shadow.epoll_create1(flags);
    } else {
        ENSURE(real, "", epoll_create1);
        return director.real.epoll_create1(flags);
    }
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", epoll_ctl);
        return director.shadow.epoll_ctl(epfd, op, fd, event);
    } else {
        ENSURE(real, "", epoll_ctl);
        return director.real.epoll_ctl(epfd, op, fd, event);
    }
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", epoll_wait);
        return director.shadow.epoll_wait(epfd, events, maxevents, timeout);
    } else {
        ENSURE(real, "", epoll_wait);
        return director.real.epoll_wait(epfd, events, maxevents, timeout);
    }
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", epoll_pwait);
        return director.shadow.epoll_pwait(epfd, events, maxevents, timeout, ss);
    } else {
        ENSURE(real, "", epoll_pwait);
        return director.real.epoll_pwait(epfd, events, maxevents, timeout, ss);
    }
}

/* socket/io family */

int socket(int domain, int type, int protocol) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", socket);
        return director.shadow.socket(domain, type, protocol);
    } else {
        ENSURE(real, "", socket);
        return director.real.socket(domain, type, protocol);
    }
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", socketpair);
        return director.shadow.socketpair(domain, type, protocol, fds);
    } else {
        ENSURE(real, "", socketpair);
        return director.real.socketpair(domain, type, protocol, fds);
    }
}

int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", bind);
        return director.shadow.bind(fd, addr, len);
    } else {
        ENSURE(real, "", bind);
        return director.real.bind(fd, addr, len);
    }
}

int getsockname(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", getsockname);
        return director.shadow.getsockname(fd, addr, len);
    } else {
        ENSURE(real, "", getsockname);
        return director.real.getsockname(fd, addr, len);
    }
}

int connect(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", connect);
        return director.shadow.connect(fd, addr, len);
    } else {
        ENSURE(real, "", connect);
        return director.real.connect(fd, addr, len);
    }
}

int getpeername(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", getpeername);
        return director.shadow.getpeername(fd, addr, len);
    } else {
        ENSURE(real, "", getpeername);
        return director.real.getpeername(fd, addr, len);
    }
}

ssize_t send(int fd, __const void *buf, size_t n, int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", send);
        return director.shadow.send(fd, buf, n, flags);
    } else {
        ENSURE(real, "", send);
        return director.real.send(fd, buf, n, flags);
    }
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags, __CONST_SOCKADDR_ARG addr, socklen_t addr_len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", sendto);
        return director.shadow.sendto(fd, buf, n, flags, addr, addr_len);
    } else {
        ENSURE(real, "", sendto);
        return director.real.sendto(fd, buf, n, flags, addr, addr_len);
    }
}

ssize_t sendmsg(int fd, __const struct msghdr *message, int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", sendmsg);
        return director.shadow.sendmsg(fd, message, flags);
    } else {
        ENSURE(real, "", sendmsg);
        return director.real.sendmsg(fd, message, flags);
    }
}

ssize_t recv(int fd, void *buf, size_t n, int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", recv);
        return director.shadow.recv(fd, buf, n, flags);
    } else {
        ENSURE(real, "", recv);
        return director.real.recv(fd, buf, n, flags);
    }
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, __SOCKADDR_ARG addr, socklen_t *restrict addr_len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", recvfrom);
        return director.shadow.recvfrom(fd, buf, n, flags, addr, addr_len);
    } else {
        ENSURE(real, "", recvfrom);
        return director.real.recvfrom(fd, buf, n, flags, addr, addr_len);
    }
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", recvmsg);
        return director.shadow.recvmsg(fd, message, flags);
    } else {
        ENSURE(real, "", recvmsg);
        return director.real.recvmsg(fd, message, flags);
    }
}

int getsockopt(int fd, int level, int optname, void *__restrict optval, socklen_t *__restrict optlen) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", getsockopt);
        return director.shadow.getsockopt(fd, level, optname, optval, optlen);
    } else {
        ENSURE(real, "", getsockopt);
        return director.real.getsockopt(fd, level, optname, optval, optlen);
    }
}

int setsockopt(int fd, int level, int optname, __const void *optval, socklen_t optlen) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", setsockopt);
        return director.shadow.setsockopt(fd, level, optname, optval, optlen);
    } else {
        ENSURE(real, "", setsockopt);
        return director.real.setsockopt(fd, level, optname, optval, optlen);
    }
}

int listen(int fd, int n) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", listen);
        return director.shadow.listen(fd, n);
    } else {
        ENSURE(real, "", listen);
        return director.real.listen(fd, n);
    }
}

int accept(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict addr_len)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", accept);
        return director.shadow.accept(fd, addr, addr_len);
    } else {
        ENSURE(real, "", accept);
        return director.real.accept(fd, addr, addr_len);
    }
}

int accept4(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict addr_len, int flags)  {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", accept4);
        return director.shadow.accept4(fd, addr, addr_len, flags);
    } else {
        ENSURE(real, "", accept4);
        return director.real.accept4(fd, addr, addr_len, flags);
    }
}

int shutdown(int fd, int how) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", shutdown);
        return director.shadow.shutdown(fd, how);
    } else {
        ENSURE(real, "", shutdown);
        return director.real.shutdown(fd, how);
    }
}

ssize_t read(int fd, void *buff, size_t numbytes) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", read);
        return director.shadow.read(fd, buff, numbytes);
    } else {
        ENSURE(real, "", read);
        return director.real.read(fd, buff, numbytes);
    }
}

ssize_t write(int fd, const void *buff, size_t n) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", write);
        return director.shadow.write(fd, buff, n);
    } else {
        ENSURE(real, "", write);
        return director.real.write(fd, buff, n);
    }
}

int close(int fd) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", close);
        return director.shadow.close(fd);
    } else {
        ENSURE(real, "", close);
        return director.real.close(fd);
    }
}

int fcntl(int fd, int cmd, ...) {
    va_list farg;
    va_start(farg, cmd);
    int result = 0;
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", fcntl);
        result = director.shadow.fcntl(fd, cmd, va_arg(farg, void*));
    } else {
        ENSURE(real, "", fcntl);
        result = director.real.fcntl(fd, cmd, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}

int ioctl(int fd, unsigned long int request, ...) {
    va_list farg;
    va_start(farg, request);
    int result = 0;
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", ioctl);
        result = director.shadow.ioctl(fd, request, va_arg(farg, void*));
    } else {
        ENSURE(real, "", ioctl);
        result = director.real.ioctl(fd, request, va_arg(farg, void*));
    }
    va_end(farg);
    return result;
}


int pipe(int pipefd[2]) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", pipe);
        return director.shadow.pipe(pipefd);
    } else {
        ENSURE(real, "", pipe);
        return director.real.pipe(pipefd);
    }
}

int pipe2(int pipefd[2], int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", pipe2);
        return director.shadow.pipe2(pipefd, flags);
    } else {
        ENSURE(real, "", pipe2);
        return director.real.pipe2(pipefd, flags);
    }
}

/* file specific */

int fileno(FILE *stream) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", fileno);
        return director.shadow.fileno(stream);
    } else {
        ENSURE(real, "", fileno);
        return director.real.fileno(stream);
    }
}

int open(const char *pathname, int flags, mode_t mode) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", open);
        return director.shadow.open(pathname, flags, mode);
    } else {
        ENSURE(real, "", open);
        return director.real.open(pathname, flags, mode);
    }
}

int open64(const char *pathname, int flags, mode_t mode) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", open);
        return director.shadow.open(pathname, flags, mode);
    } else {
        ENSURE(real, "", open64);
        return director.real.open64(pathname, flags, mode);
    }
}

int creat(const char *pathname, mode_t mode) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", creat);
        return director.shadow.creat(pathname, mode);
    } else {
        ENSURE(real, "", creat);
        return director.real.creat(pathname, mode);
    }
}

FILE *fopen(const char *path, const char *mode) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", fopen);
        return director.shadow.fopen(path, mode);
    } else {
        ENSURE(real, "", fopen);
        return director.real.fopen(path, mode);
    }
}

FILE *fdopen(int fd, const char *mode) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", fdopen);
        return director.shadow.fdopen(fd, mode);
    } else {
        ENSURE(real, "", fdopen);
        return director.real.fdopen(fd, mode);
    }
}

int dup(int oldfd) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", dup);
        return director.shadow.dup(oldfd);
    } else {
        ENSURE(real, "", dup);
        return director.real.dup(oldfd);
    }
}

int dup2(int oldfd, int newfd) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", dup2);
        return director.shadow.dup2(oldfd, newfd);
    } else {
        ENSURE(real, "", dup2);
        return director.real.dup2(oldfd, newfd);
    }
}

int dup3(int oldfd, int newfd, int flags) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", dup3);
        return director.shadow.dup3(oldfd, newfd, flags);
    } else {
        ENSURE(real, "", dup3);
        return director.real.dup3(oldfd, newfd, flags);
    }
}

int fclose(FILE *fp) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", fclose);
        return director.shadow.fclose(fp);
    } else {
        ENSURE(real, "", fclose);
        return director.real.fclose(fp);
    }
}

/* fstat redirects to this */
int __fxstat (int ver, int fd, struct stat *buf) {
    if (shouldRedirect()) {
        ENSURE(shadow, "intercept_", __fxstat);
        return director.shadow.__fxstat(ver, fd, buf);
    } else {
        ENSURE(real, "", __fxstat);
        return director.real.__fxstat(ver, fd, buf);
    }
}

int fstatfs (int fd, struct statfs *buf) {
    if (shouldRedirect()) {
        ENSURE(shadow, "intercept_", fstatfs);
        return director.shadow.fstatfs(fd, buf);
    } else {
        ENSURE(real, "", fstatfs);
        return director.real.fstatfs(fd, buf);
    }
}

off_t lseek(int fd, off_t offset, int whence) {
    if (shouldRedirect()) {
        ENSURE(shadow, "intercept_", lseek);
        return director.shadow.lseek(fd, offset, whence);
    } else {
        ENSURE(real, "", lseek);
        return director.real.lseek(fd, offset, whence);
    }
}

int flock(int fd, int operation) {
    if (shouldRedirect()) {
        ENSURE(shadow, "intercept_", flock);
        return director.shadow.flock(fd, operation);
    } else {
        ENSURE(real, "", flock);
        return director.real.flock(fd, operation);
    }
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
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", time);
        return director.shadow.time(t);
    } else {
        ENSURE(real, "", time);
        return director.real.time(t);
    }
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", clock_gettime);
        return director.shadow.clock_gettime(clk_id, tp);
    } else {
        ENSURE(real, "", clock_gettime);
        return director.real.clock_gettime(clk_id, tp);
    }
}

int gettimeofday(struct timeval* tv, __timezone_ptr_t tz) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gettimeofday);
        return director.shadow.gettimeofday(tv, tz);
    } else {
        ENSURE(real, "", gettimeofday);
        return director.real.gettimeofday(tv, tz);
    }
}


/* name/address family */


int gethostname(char* name, size_t len) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostname);
        return director.shadow.gethostname(name, len);
    } else {
        ENSURE(real, "", gethostname);
        return director.real.gethostname(name, len);
    }
}

int getaddrinfo(const char *node, const char *service,
const struct addrinfo *hints, struct addrinfo **res) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", getaddrinfo);
        return director.shadow.getaddrinfo(node, service, hints, res);
    } else {
        ENSURE(real, "", getaddrinfo);
        return director.real.getaddrinfo(node, service, hints, res);
    }
}

void freeaddrinfo(struct addrinfo *res) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", freeaddrinfo);
        director.shadow.freeaddrinfo(res);
    } else {
        ENSURE(real, "", freeaddrinfo);
        director.real.freeaddrinfo(res);
    }
}

int getnameinfo(const struct sockaddr *__restrict sa, socklen_t salen,
		char *__restrict host, socklen_t hostlen, char *__restrict serv,
		socklen_t servlen,
		/* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
		int flags) {
#else
		unsigned int flags) {
#endif

	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", getnameinfo);
		return director.shadow.getnameinfo(sa, salen, host, hostlen, serv, servlen, (int)flags);
	} else {
		ENSURE(real, "", getnameinfo);
		return director.real.getnameinfo(sa, salen, host, hostlen, serv, servlen, (int)flags);
	}
}

struct hostent* gethostbyname(const gchar* name) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyname);
        return director.shadow.gethostbyname(name);
    } else {
        ENSURE(real, "", gethostbyname);
        return director.real.gethostbyname(name);
    }
}

int gethostbyname_r(const gchar *name, struct hostent *ret, gchar *buf,
gsize buflen, struct hostent **result, gint *h_errnop) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyname_r);
        return director.shadow.gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
    } else {
        ENSURE(real, "", gethostbyname_r);
        return director.real.gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
    }
}

struct hostent* gethostbyname2(const gchar* name, gint af) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyname2);
        return director.shadow.gethostbyname2(name, af);
    } else {
        ENSURE(real, "", gethostbyname2);
        return director.real.gethostbyname2(name, af);
    }
}

int gethostbyname2_r(const gchar *name, gint af, struct hostent *ret,
		gchar *buf, gsize buflen, struct hostent **result, gint *h_errnop) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyname2_r);
        return director.shadow.gethostbyname2_r(name, af, ret, buf, buflen, result, h_errnop);
    } else {
        ENSURE(real, "", gethostbyname2_r);
        return director.real.gethostbyname2_r(name, af, ret, buf, buflen, result, h_errnop);
    }
}

struct hostent* gethostbyaddr(const void* addr, socklen_t len, gint type) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyaddr);
        return director.shadow.gethostbyaddr(addr, len, type);
    } else {
        ENSURE(real, "", gethostbyaddr);
        return director.real.gethostbyaddr(addr, len, type);
    }
}

int gethostbyaddr_r(const void *addr, socklen_t len, gint type,
struct hostent *ret, char *buf, gsize buflen, struct hostent **result,
gint *h_errnop) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", gethostbyaddr_r);
        return director.shadow.gethostbyaddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
    } else {
        ENSURE(real, "", gethostbyaddr_r);
        return director.real.gethostbyaddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
    }
}

/* random family */

int rand() {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", rand);
        return director.shadow.rand();
    } else {
        ENSURE(real, "", rand);
        return director.real.rand();
    }
}

int rand_r(unsigned int *seedp) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", rand_r);
        return director.shadow.rand_r(seedp);
    } else {
        ENSURE(real, "", rand_r);
        return director.real.rand_r(seedp);
    }
}

void srand(unsigned int seed) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", srand);
        director.shadow.srand(seed);
    } else {
        ENSURE(real, "", srand);
        director.real.srand(seed);
    }
}

long int random() {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", random);
        return director.shadow.random();
    } else {
        ENSURE(real, "", random);
        return director.real.random();
    }
}

int random_r(struct random_data *buf, int32_t *result) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", random_r);
        return director.shadow.random_r(buf, result);
    } else {
        ENSURE(real, "", random_r);
        return director.real.random_r(buf, result);
    }
}

void srandom(unsigned int seed) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", srandom);
        director.shadow.srandom(seed);
    } else {
        ENSURE(real, "", srandom);
        director.real.srandom(seed);
    }
}

int srandom_r(unsigned int seed, struct random_data *buf) {
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", srandom_r);
        return director.shadow.srandom_r(seed, buf);
    } else {
        ENSURE(real, "", srandom_r);
        return director.real.srandom_r(seed, buf);
    }
}

/* openssl family
 * these functions are lazily loaded to ensure the symbol exists when searching.
 * this is necessary because openssl is dynamically loaded as part of plugin code */

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", AES_encrypt);
		director.shadow.AES_encrypt(in, out, key);
	} else {
		ENSURE(real, "", AES_encrypt);
		director.real.AES_encrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", AES_decrypt);
		director.shadow.AES_decrypt(in, out, key);
	} else {
		ENSURE(real, "", AES_decrypt);
		director.real.AES_decrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", AES_ctr128_encrypt);
		director.shadow.AES_ctr128_encrypt(in, out, key);
	} else {
		ENSURE(real, "", AES_ctr128_encrypt);
		director.real.AES_ctr128_encrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", AES_ctr128_decrypt);
		director.shadow.AES_ctr128_decrypt(in, out, key);
	} else {
		ENSURE(real, "", AES_ctr128_decrypt);
		director.real.AES_ctr128_decrypt(in, out, key);
	}
}

/*
 * There is a corner case on certain machines that causes padding-related errors
 * when the EVP_Cipher is set to use aesni_cbc_hmac_sha1_cipher. Our memmove
 * implementation does not handle padding.
 *
 * We attempt to disable the use of aesni_cbc_hmac_sha1_cipher with the environment
 * variable OPENSSL_ia32cap=~0x200000200000000, and by default intercept EVP_Cipher
 * in order to skip the encryption.
 *
 * If that doesn't work, the user can request that we let the application perform
 * the encryption by defining SHADOW_ENABLE_EVPCIPHER, which means we will not
 * intercept EVP_Cipher and instead let OpenSSL do its thing.
 */
#ifndef SHADOW_ENABLE_EVPCIPHER
/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
int EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl){
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", EVP_Cipher);
		return director.shadow.EVP_Cipher(ctx, out, in, inl);
	} else {
		ENSURE(real, "", EVP_Cipher);
		return director.real.EVP_Cipher(ctx, out, in, inl);
	}
}
#endif

void* CRYPTO_get_locking_callback() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", CRYPTO_get_locking_callback);
		return director.shadow.CRYPTO_get_locking_callback();
	} else {
		ENSURE(real, "", CRYPTO_get_locking_callback);
		return director.real.CRYPTO_get_locking_callback();
	}
}

void* CRYPTO_get_id_callback() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", CRYPTO_get_id_callback);
		return director.shadow.CRYPTO_get_id_callback();
	} else {
		ENSURE(real, "", CRYPTO_get_id_callback);
		return director.real.CRYPTO_get_id_callback();
	}
}

void RAND_seed(const void *buf, int num) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_seed);
		director.shadow.RAND_seed(buf, num);
	} else {
		ENSURE(real, "", RAND_seed);
		director.real.RAND_seed(buf, num);
	}
}

void RAND_add(const void *buf, int num, double entropy) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_add);
		director.shadow.RAND_add(buf, num, entropy);
	} else {
		ENSURE(real, "", RAND_add);
		director.real.RAND_add(buf, num, entropy);
	}
}

int RAND_poll() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_poll);
		return director.shadow.RAND_poll();
	} else {
		ENSURE(real, "", RAND_poll);
		return director.real.RAND_poll();
	}
}

int RAND_bytes(unsigned char *buf, int num) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_bytes);
		return director.shadow.RAND_bytes(buf, num);
	} else {
		ENSURE(real, "", RAND_bytes);
		return director.real.RAND_bytes(buf, num);
	}
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_pseudo_bytes);
		return director.shadow.RAND_pseudo_bytes(buf, num);
	} else {
		ENSURE(real, "", RAND_pseudo_bytes);
		return director.real.RAND_pseudo_bytes(buf, num);
	}
}

void RAND_cleanup() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_cleanup);
		director.shadow.RAND_cleanup();
	} else {
		ENSURE(real, "", RAND_cleanup);
		director.real.RAND_cleanup();
	}
}

int RAND_status() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_status);
		return director.shadow.RAND_status();
	} else {
		ENSURE(real, "", RAND_status);
		return director.real.RAND_status();
	}
}

const void *RAND_get_rand_method() {
	if(shouldRedirect()) {
		ENSURE(shadow, "intercept_", RAND_get_rand_method);
		return director.shadow.RAND_get_rand_method();
	} else {
		ENSURE(real, "", RAND_get_rand_method);
		return director.real.RAND_get_rand_method();
	}
}

const void* RAND_SSLeay() {
    /* return the same thing as RAND_get_rand_method */
    if(shouldRedirect()) {
        ENSURE(shadow, "intercept_", RAND_get_rand_method);
        return director.shadow.RAND_get_rand_method();
    } else {
        ENSURE(real, "", RAND_get_rand_method);
        return director.real.RAND_get_rand_method();
    }
}
