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
typedef size_t (*ReadFunc)(int, void*, int);
typedef size_t (*WriteFunc)(int, const void*, int);
typedef int (*CloseFunc)(int);
typedef int (*FcntlFunc)(int, int, ...);

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
		char buf[10240];
		unsigned long pos;
		unsigned long nallocs;
		unsigned long ndeallocs;
	} dummy;
	PreloadFuncs real;
	PreloadFuncs shadow;
	WorkerIsInShadowContextFunc isShadowFunc;
} FuncDirector;

/* the director is initialized at library load time before
 * threads are created, and once initialized it is shared among all threads.
 *
 * the openssl functions will be NULL until they are called, at which point the
 * threads will do the lookup and set the function pointers. this part is not really
 * thread-safe, but if two threads lookup the same function at the same time, the
 * resulting pointer will be identical anyway - so we avoid protecting it.
 *
 * if this becomes a problem, we can make the director thread-local (__thread), initialize
 * it separately in each thread, and then each thread will have its own function state.
 * in this case, threads should access it ONLY via a &director pointer lookup, which is how
 * they get their copy of the data.
 * http://gcc.gnu.org/onlinedocs/gcc-4.3.6/gcc/Thread_002dLocal.html
 */
static FuncDirector director;

/* whether or not we are in a recursive loop is thread-specific, since each thread
 * will have their own call stacks.
 *
 * threads MUST access this via &isRecursive to ensure it has its own copy
 * http://gcc.gnu.org/onlinedocs/gcc-4.3.6/gcc/Thread_002dLocal.html*/
static __thread unsigned long isRecursive = 0;

static void* dummy_malloc(size_t size) {
    if (director.dummy.pos + size >= sizeof(director.dummy.buf)) {
    	exit(EXIT_FAILURE);
    }
    void *retptr = director.dummy.buf + director.dummy.pos;
    director.dummy.pos += size;
    ++director.dummy.nallocs;
    return retptr;
}

static void* dummy_calloc(size_t nmemb, size_t size) {
    void *ptr = dummy_malloc(nmemb * size);
    unsigned int i = 0;
    for (; i < nmemb * size; ++i) {
        *((char*)(ptr + i)) = '\0';
    }
    return ptr;
}

static void dummy_free(void *ptr) {
	++director.dummy.ndeallocs;
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

	SETSYM_OR_FAIL(director.real.realloc, "realloc");
	SETSYM_OR_FAIL(director.real.posix_memalign, "posix_memalign");
	SETSYM_OR_FAIL(director.real.memalign, "memalign");
	SETSYM_OR_FAIL(director.real.aligned_alloc, "aligned_alloc");
	SETSYM_OR_FAIL(director.real.valloc, "valloc");
	SETSYM_OR_FAIL(director.real.pvalloc, "pvalloc");

	SETSYM_OR_FAIL(director.real.epoll_create, "epoll_create");
	SETSYM_OR_FAIL(director.real.epoll_create1, "epoll_create1");
	SETSYM_OR_FAIL(director.real.epoll_ctl, "epoll_ctl");
	SETSYM_OR_FAIL(director.real.epoll_wait, "epoll_wait");
	SETSYM_OR_FAIL(director.real.epoll_pwait, "epoll_pwait");

	SETSYM_OR_FAIL(director.real.socket, "socket");
	SETSYM_OR_FAIL(director.real.socketpair, "socketpair");
	SETSYM_OR_FAIL(director.real.bind, "bind");
	SETSYM_OR_FAIL(director.real.getsockname, "getsockname");
	SETSYM_OR_FAIL(director.real.connect, "connect");
	SETSYM_OR_FAIL(director.real.getpeername, "getpeername");
	SETSYM_OR_FAIL(director.real.send, "send");
	SETSYM_OR_FAIL(director.real.sendto, "sendto");
	SETSYM_OR_FAIL(director.real.sendmsg, "sendmsg");
	SETSYM_OR_FAIL(director.real.recv, "recv");
	SETSYM_OR_FAIL(director.real.recvfrom, "recvfrom");
	SETSYM_OR_FAIL(director.real.recvmsg, "recvmsg");
	SETSYM_OR_FAIL(director.real.getsockopt, "getsockopt");
	SETSYM_OR_FAIL(director.real.setsockopt, "setsockopt");
	SETSYM_OR_FAIL(director.real.listen, "listen");
	SETSYM_OR_FAIL(director.real.accept, "accept");
	SETSYM_OR_FAIL(director.real.accept4, "accept4");
	SETSYM_OR_FAIL(director.real.shutdown, "shutdown");
	SETSYM_OR_FAIL(director.real.pipe, "pipe");
	SETSYM_OR_FAIL(director.real.pipe2, "pipe2");
	SETSYM_OR_FAIL(director.real.read, "read");
	SETSYM_OR_FAIL(director.real.write, "write");
	SETSYM_OR_FAIL(director.real.close, "close");
	SETSYM_OR_FAIL(director.real.fcntl, "fcntl");

	SETSYM_OR_FAIL(director.real.time, "time");
	SETSYM_OR_FAIL(director.real.clock_gettime, "clock_gettime");
	SETSYM_OR_FAIL(director.real.gettimeofday, "gettimeofday");

	SETSYM_OR_FAIL(director.real.gethostname, "gethostname");
	SETSYM_OR_FAIL(director.real.getaddrinfo, "getaddrinfo");
	SETSYM_OR_FAIL(director.real.freeaddrinfo, "freeaddrinfo");
	SETSYM_OR_FAIL(director.real.getnameinfo, "getnameinfo");
	SETSYM_OR_FAIL(director.real.gethostbyname, "gethostbyname");
	SETSYM_OR_FAIL(director.real.gethostbyname_r, "gethostbyname_r");
	SETSYM_OR_FAIL(director.real.gethostbyname2, "gethostbyname2");
	SETSYM_OR_FAIL(director.real.gethostbyname2_r, "gethostbyname2_r");
	SETSYM_OR_FAIL(director.real.gethostbyaddr, "gethostbyaddr");
	SETSYM_OR_FAIL(director.real.gethostbyaddr_r, "gethostbyaddr_r");

	SETSYM_OR_FAIL(director.real.rand, "rand");
	SETSYM_OR_FAIL(director.real.rand_r, "rand_r");
	SETSYM_OR_FAIL(director.real.srand, "srand");
	SETSYM_OR_FAIL(director.real.random, "random");
	SETSYM_OR_FAIL(director.real.random_r, "random_r");
	SETSYM_OR_FAIL(director.real.srandom, "srandom");
	SETSYM_OR_FAIL(director.real.srandom_r, "srandom_r");

	/* openssl funcs are missing above because we must search those lazily
	 * since they are part of a dynamic library plugin that is not
	 * loaded at the time shadow starts. if we ever preload other dynamic
	 * library funcs, the same technique should be used.
	 */

	SETSYM_OR_FAIL(director.shadow.malloc, "intercept_malloc");
	SETSYM_OR_FAIL(director.shadow.calloc, "intercept_calloc");
	SETSYM_OR_FAIL(director.shadow.realloc, "intercept_realloc");
	SETSYM_OR_FAIL(director.shadow.posix_memalign, "intercept_posix_memalign");
	SETSYM_OR_FAIL(director.shadow.memalign, "intercept_memalign");
	SETSYM_OR_FAIL(director.shadow.aligned_alloc, "intercept_aligned_alloc");
	SETSYM_OR_FAIL(director.shadow.valloc, "intercept_valloc");
	SETSYM_OR_FAIL(director.shadow.pvalloc, "intercept_pvalloc");
	SETSYM_OR_FAIL(director.shadow.free, "intercept_free");

	SETSYM_OR_FAIL(director.shadow.epoll_create, "intercept_epoll_create");
	SETSYM_OR_FAIL(director.shadow.epoll_create1, "intercept_epoll_create1");
	SETSYM_OR_FAIL(director.shadow.epoll_ctl, "intercept_epoll_ctl");
	SETSYM_OR_FAIL(director.shadow.epoll_wait, "intercept_epoll_wait");
	SETSYM_OR_FAIL(director.shadow.epoll_pwait, "intercept_epoll_pwait");

	SETSYM_OR_FAIL(director.shadow.socket, "intercept_socket");
	SETSYM_OR_FAIL(director.shadow.socketpair, "intercept_socketpair");
	SETSYM_OR_FAIL(director.shadow.bind, "intercept_bind");
	SETSYM_OR_FAIL(director.shadow.getsockname, "intercept_getsockname");
	SETSYM_OR_FAIL(director.shadow.connect, "intercept_connect");
	SETSYM_OR_FAIL(director.shadow.getpeername, "intercept_getpeername");
	SETSYM_OR_FAIL(director.shadow.send, "intercept_send");
	SETSYM_OR_FAIL(director.shadow.sendto, "intercept_sendto");
	SETSYM_OR_FAIL(director.shadow.sendmsg, "intercept_sendmsg");
	SETSYM_OR_FAIL(director.shadow.recv, "intercept_recv");
	SETSYM_OR_FAIL(director.shadow.recvfrom, "intercept_recvfrom");
	SETSYM_OR_FAIL(director.shadow.recvmsg, "intercept_recvmsg");
	SETSYM_OR_FAIL(director.shadow.getsockopt, "intercept_getsockopt");
	SETSYM_OR_FAIL(director.shadow.setsockopt, "intercept_setsockopt");
	SETSYM_OR_FAIL(director.shadow.listen, "intercept_listen");
	SETSYM_OR_FAIL(director.shadow.accept, "intercept_accept");
	SETSYM_OR_FAIL(director.shadow.accept4, "intercept_accept4");
	SETSYM_OR_FAIL(director.shadow.shutdown, "intercept_shutdown");
	SETSYM_OR_FAIL(director.shadow.pipe, "intercept_pipe");
	SETSYM_OR_FAIL(director.shadow.pipe2, "intercept_pipe2");
	SETSYM_OR_FAIL(director.shadow.read, "intercept_read");
	SETSYM_OR_FAIL(director.shadow.write, "intercept_write");
	SETSYM_OR_FAIL(director.shadow.close, "intercept_close");
	SETSYM_OR_FAIL(director.shadow.fcntl, "intercept_fcntl");

	SETSYM_OR_FAIL(director.shadow.time, "intercept_time");
	SETSYM_OR_FAIL(director.shadow.clock_gettime, "intercept_clock_gettime");
	SETSYM_OR_FAIL(director.shadow.gettimeofday, "intercept_gettimeofday");

	SETSYM_OR_FAIL(director.shadow.gethostname, "intercept_gethostname");
	SETSYM_OR_FAIL(director.shadow.getaddrinfo, "intercept_getaddrinfo");
	SETSYM_OR_FAIL(director.shadow.freeaddrinfo, "intercept_freeaddrinfo");
	SETSYM_OR_FAIL(director.shadow.getnameinfo, "intercept_getnameinfo");
	SETSYM_OR_FAIL(director.shadow.gethostbyname, "intercept_gethostbyname");
	SETSYM_OR_FAIL(director.shadow.gethostbyname_r, "intercept_gethostbyname_r");
	SETSYM_OR_FAIL(director.shadow.gethostbyname2, "intercept_gethostbyname2");
	SETSYM_OR_FAIL(director.shadow.gethostbyname2_r, "intercept_gethostbyname2_r");
	SETSYM_OR_FAIL(director.shadow.gethostbyaddr, "intercept_gethostbyaddr");
	SETSYM_OR_FAIL(director.shadow.gethostbyaddr_r, "intercept_gethostbyaddr_r");

	SETSYM_OR_FAIL(director.shadow.AES_encrypt, "intercept_AES_encrypt");
	SETSYM_OR_FAIL(director.shadow.AES_decrypt, "intercept_AES_decrypt");
	SETSYM_OR_FAIL(director.shadow.AES_ctr128_encrypt, "intercept_AES_ctr128_encrypt");
	SETSYM_OR_FAIL(director.shadow.AES_ctr128_decrypt, "intercept_AES_ctr128_decrypt");
	SETSYM_OR_FAIL(director.shadow.EVP_Cipher, "intercept_EVP_Cipher");
	SETSYM_OR_FAIL(director.shadow.CRYPTO_get_locking_callback, "intercept_CRYPTO_get_locking_callback");
	SETSYM_OR_FAIL(director.shadow.CRYPTO_get_id_callback, "intercept_CRYPTO_get_id_callback");

	SETSYM_OR_FAIL(director.shadow.RAND_seed, "intercept_RAND_seed");
	SETSYM_OR_FAIL(director.shadow.RAND_add, "intercept_RAND_add");
	SETSYM_OR_FAIL(director.shadow.RAND_poll, "intercept_RAND_poll");
	SETSYM_OR_FAIL(director.shadow.RAND_bytes, "intercept_RAND_bytes");
	SETSYM_OR_FAIL(director.shadow.RAND_pseudo_bytes, "intercept_RAND_pseudo_bytes");
	SETSYM_OR_FAIL(director.shadow.RAND_cleanup, "intercept_RAND_cleanup");
	SETSYM_OR_FAIL(director.shadow.RAND_status, "intercept_RAND_status");
	SETSYM_OR_FAIL(director.shadow.RAND_get_rand_method, "intercept_RAND_get_rand_method");

	SETSYM_OR_FAIL(director.shadow.rand, "intercept_rand");
	SETSYM_OR_FAIL(director.shadow.rand_r, "intercept_rand_r");
	SETSYM_OR_FAIL(director.shadow.srand, "intercept_srand");
	SETSYM_OR_FAIL(director.shadow.random, "intercept_random");
	SETSYM_OR_FAIL(director.shadow.random_r, "intercept_random_r");
	SETSYM_OR_FAIL(director.shadow.srandom, "intercept_srandom");
	SETSYM_OR_FAIL(director.shadow.srandom_r, "intercept_srandom_r");

	SETSYM_OR_FAIL(director.isShadowFunc, "intercept_worker_isInShadowContext");

    __sync_fetch_and_sub(&isRecursive, 1);
}

static inline int shouldRedirect() {
	int doRedirect = 0;
	/* recursive calls always go to the syscall */
	if(!__sync_fetch_and_add(&isRecursive, 1)) {
		/* ask shadow if this call is a plug-in that should be intercepted */
		doRedirect = director.isShadowFunc() ? 0 : 1;
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

#ifdef SHADOW_ENABLE_MEMTRACKER
void* malloc(size_t size) {
    return shouldRedirect() ?
    		director.shadow.malloc(size) :
    		director.real.malloc(size);
}

void* calloc(size_t nmemb, size_t size) {
    return shouldRedirect() ?
    		director.shadow.calloc(nmemb, size) :
    		director.real.calloc(nmemb, size);
}

void* realloc(void *ptr, size_t size) {
    return shouldRedirect() ?
    		director.shadow.realloc(ptr, size) :
    		director.real.realloc(ptr, size);
}

void free(void *ptr) {
    shouldRedirect() ?
    		director.shadow.free(ptr) :
    		director.real.free(ptr);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    return shouldRedirect() ?
    		director.shadow.posix_memalign(memptr, alignment, size) :
    		director.real.posix_memalign(memptr, alignment, size);
}

void* memalign(size_t blocksize, size_t bytes) {
    return shouldRedirect() ?
    		director.shadow.memalign(blocksize, bytes) :
    		director.real.memalign(blocksize, bytes);
}

void* aligned_alloc(size_t alignment, size_t size) {
	return shouldRedirect() ?
			director.shadow.aligned_alloc(alignment, size) :
			director.real.aligned_alloc(alignment, size);
}

void* valloc(size_t size) {
    return shouldRedirect() ?
    		director.shadow.valloc(size) :
    		director.real.valloc(size);
}

void* pvalloc(size_t size) {
	return shouldRedirect() ?
			director.shadow.pvalloc(size) :
			director.real.pvalloc(size);
}
#endif

/* event family */

int epoll_create(int size) {
	return shouldRedirect() ?
			director.shadow.epoll_create(size) :
			director.real.epoll_create(size);
}

int epoll_create1(int flags) {
	return shouldRedirect() ?
			director.shadow.epoll_create1(flags) :
			director.real.epoll_create1(flags);
}

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
	return shouldRedirect() ?
			director.shadow.epoll_ctl(epfd, op, fd, event) :
			director.real.epoll_ctl(epfd, op, fd, event);
}

int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
	return shouldRedirect() ?
			director.shadow.epoll_wait(epfd, events, maxevents, timeout) :
			director.real.epoll_wait(epfd, events, maxevents, timeout);
}

int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss) {
	return shouldRedirect() ?
			director.shadow.epoll_pwait(epfd, events, maxevents, timeout, ss) :
			director.real.epoll_pwait(epfd, events, maxevents, timeout, ss);
}

/* socket/io family */

int socket(int domain, int type, int protocol) {
	return shouldRedirect() ?
			director.shadow.socket(domain, type, protocol) :
			director.real.socket(domain, type, protocol);
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
	return shouldRedirect() ?
			director.shadow.socketpair(domain, type, protocol, fds) :
			director.real.socketpair(domain, type, protocol, fds);
}

int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.bind(fd, addr, len) :
			director.real.bind(fd, addr, len);
}

int getsockname(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.getsockname(fd, addr, len) :
			director.real.getsockname(fd, addr, len);
}

int connect(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.connect(fd, addr, len) :
			director.real.connect(fd, addr, len);
}

int getpeername(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.getpeername(fd, addr, len) :
			director.real.getpeername(fd, addr, len);
}

ssize_t send(int fd, __const void *buf, size_t n, int flags) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.send(fd, buf, n, flags) :
			director.real.send(fd, buf, n, flags);
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags, __CONST_SOCKADDR_ARG addr, socklen_t addr_len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.sendto(fd, buf, n, flags, addr, addr_len) :
			director.real.sendto(fd, buf, n, flags, addr, addr_len);
}

ssize_t sendmsg(int fd, __const struct msghdr *message, int flags) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.sendmsg(fd, message, flags) :
			director.real.sendmsg(fd, message, flags);
}

ssize_t recv(int fd, void *buf, size_t n, int flags) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.recv(fd, buf, n, flags) :
			director.real.recv(fd, buf, n, flags);
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, __SOCKADDR_ARG addr, socklen_t *restrict addr_len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.recvfrom(fd, buf, n, flags, addr, addr_len) :
			director.real.recvfrom(fd, buf, n, flags, addr, addr_len);
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.recvmsg(fd, message, flags) :
			director.real.recvmsg(fd, message, flags);
}

int getsockopt(int fd, int level, int optname, void *__restrict optval, socklen_t *__restrict optlen) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.getsockopt(fd, level, optname, optval, optlen) :
			director.real.getsockopt(fd, level, optname, optval, optlen);
}

int setsockopt(int fd, int level, int optname, __const void *optval, socklen_t optlen) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.setsockopt(fd, level, optname, optval, optlen) :
			director.real.setsockopt(fd, level, optname, optval, optlen);
}

int listen(int fd, int n) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.listen(fd, n) :
			director.real.listen(fd, n);
}

int accept(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict addr_len)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.accept(fd, addr, addr_len) :
			director.real.accept(fd, addr, addr_len);
}

int accept4(int fd, __SOCKADDR_ARG addr, socklen_t *__restrict addr_len, int flags)  {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.accept4(fd, addr, addr_len, flags) :
			director.real.accept4(fd, addr, addr_len, flags);
}

int shutdown(int fd, int how) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.shutdown(fd, how) :
			director.real.shutdown(fd, how);
}

ssize_t read(int fd, void *buff, int numbytes) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.read(fd, buff, numbytes) :
			director.real.read(fd, buff, numbytes);
}

ssize_t write(int fd, const void *buff, int n) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.write(fd, buff, n) :
			director.real.write(fd, buff, n);
}

int close(int fd) {
	return (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.close(fd) :
			director.real.close(fd);
}

int fcntl(int fd, int cmd, ...) {
	va_list farg;
	va_start(farg, cmd);
	int result = (fd >= MIN_DESCRIPTOR && shouldRedirect()) ?
			director.shadow.fcntl(fd, cmd, va_arg(farg, void*)) :
			director.real.fcntl(fd, cmd, va_arg(farg, void*));
	va_end(farg);

	return result;
}

int pipe(int pipefd[2]) {
	return shouldRedirect() ?
			director.shadow.pipe(pipefd) :
			director.real.pipe(pipefd);
}

int pipe2(int pipefd[2], int flags) {
	return shouldRedirect() ?
			director.shadow.pipe2(pipefd, flags) :
			director.real.pipe2(pipefd, flags);
}

/* time family */

time_t time(time_t *t)  {
	return shouldRedirect() ?
			director.shadow.time(t) :
			director.real.time(t);
}

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
	return shouldRedirect() ?
			director.shadow.clock_gettime(clk_id, tp) :
			director.real.clock_gettime(clk_id, tp);
}

int gettimeofday(struct timeval* tv, __timezone_ptr_t tz) {
	return shouldRedirect() ?
			director.shadow.gettimeofday(tv, tz) :
			director.real.gettimeofday(tv, tz);
}

/* name/address family */


int gethostname(char* name, size_t len) {
	return shouldRedirect() ?
			director.shadow.gethostname(name, len) :
			director.real.gethostname(name, len);
}

int getaddrinfo(const char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **res) {
	return shouldRedirect() ?
			director.shadow.getaddrinfo(node, service, hints, res) :
			director.real.getaddrinfo(node, service, hints, res);
}

void freeaddrinfo(struct addrinfo *res) {
	shouldRedirect() ?
			director.shadow.freeaddrinfo(res) :
			director.real.freeaddrinfo(res);
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
	return shouldRedirect() ?
			director.shadow.getnameinfo(sa, salen, host, hostlen, serv, servlen, (int)flags) :
			director.real.getnameinfo(sa, salen, host, hostlen, serv, servlen, (int)flags);
}

struct hostent* gethostbyname(const gchar* name) {
	return shouldRedirect() ?
			director.shadow.gethostbyname(name) :
			director.real.gethostbyname(name);
}

int gethostbyname_r(const gchar *name, struct hostent *ret, gchar *buf,
		gsize buflen, struct hostent **result, gint *h_errnop) {
	return shouldRedirect() ?
			director.shadow.gethostbyname_r(name, ret, buf, buflen, result, h_errnop) :
			director.real.gethostbyname_r(name, ret, buf, buflen, result, h_errnop);
}

struct hostent* gethostbyname2(const gchar* name, gint af) {
	return shouldRedirect() ?
			director.shadow.gethostbyname2(name, af) :
			director.real.gethostbyname2(name, af);
}

int gethostbyname2_r(const gchar *name, gint af, struct hostent *ret,
		gchar *buf, gsize buflen, struct hostent **result, gint *h_errnop) {
	return shouldRedirect() ?
			director.shadow.gethostbyname2_r(name, af, ret, buf, buflen, result, h_errnop) :
			director.real.gethostbyname2_r(name, af, ret, buf, buflen, result, h_errnop);
}

struct hostent* gethostbyaddr(const void* addr, socklen_t len, gint type) {
	return shouldRedirect() ?
			director.shadow.gethostbyaddr(addr, len, type) :
			director.real.gethostbyaddr(addr, len, type);
}

int gethostbyaddr_r(const void *addr, socklen_t len, gint type,
		struct hostent *ret, char *buf, gsize buflen, struct hostent **result,
		gint *h_errnop) {
	return shouldRedirect() ?
			director.shadow.gethostbyaddr_r(addr, len, type, ret, buf, buflen, result, h_errnop) :
			director.real.gethostbyaddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
}

/* random family */

int rand() {
	return shouldRedirect() ?
			director.shadow.rand() :
			director.real.rand();
}

int rand_r(unsigned int *seedp) {
	return shouldRedirect() ?
			director.shadow.rand_r(seedp) :
			director.real.rand_r(seedp);
}

void srand(unsigned int seed) {
	shouldRedirect() ?
			director.shadow.srand(seed) :
			director.real.srand(seed);
}

long int random() {
	return shouldRedirect() ?
			director.shadow.random() :
			director.real.random();
}

int random_r(struct random_data *buf, int32_t *result) {
	return shouldRedirect() ?
			director.shadow.random_r(buf, result) :
			director.real.random_r(buf, result);
}

void srandom(unsigned int seed) {
	shouldRedirect() ?
			director.shadow.srandom(seed) :
			director.real.srandom(seed);
}

int srandom_r(unsigned int seed, struct random_data *buf) {
	return shouldRedirect() ?
			director.shadow.srandom_r(seed, buf) :
			director.real.srandom_r(seed, buf);
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
		director.shadow.AES_encrypt(in, out, key);
	} else {
		if(!director.real.AES_encrypt)
			SETSYM_OR_FAIL(director.real.AES_encrypt, "AES_encrypt");
		director.real.AES_encrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		director.shadow.AES_decrypt(in, out, key);
	} else {
		if(!director.real.AES_decrypt)
			SETSYM_OR_FAIL(director.real.AES_decrypt, "AES_decrypt");
		director.real.AES_decrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		director.shadow.AES_ctr128_encrypt(in, out, key);
	} else {
		if(!director.real.AES_ctr128_encrypt)
			SETSYM_OR_FAIL(director.real.AES_ctr128_encrypt, "AES_ctr128_encrypt");
		director.real.AES_ctr128_encrypt(in, out, key);
	}
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	if(shouldRedirect()) {
		director.shadow.AES_ctr128_decrypt(in, out, key);
	} else {
		if(!director.real.AES_ctr128_decrypt)
			SETSYM_OR_FAIL(director.real.AES_ctr128_decrypt, "AES_ctr128_decrypt");
		director.real.AES_ctr128_decrypt(in, out, key);
	}
}

/*
 * There is a corner case on certain machines that causes padding-related errors
 * when the EVP_Cipher is set to use aesni_cbc_hmac_sha1_cipher. Our memmove
 * implementation does not handle padding, so we disable it by default.
 */
#ifdef SHADOW_ENABLE_EVPCIPHER
/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
int EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl){
	if(shouldRedirect()) {
		return director.shadow.EVP_Cipher(ctx, out, in, inl);
	} else {
		if(!director.real.EVP_Cipher)
			SETSYM_OR_FAIL(director.real.EVP_Cipher, "EVP_Cipher");
		return director.real.EVP_Cipher(ctx, out, in, inl);
	}
}
#endif

void* CRYPTO_get_locking_callback() {
	if(shouldRedirect()) {
		return director.shadow.CRYPTO_get_locking_callback();
	} else {
		if(!director.real.CRYPTO_get_locking_callback)
			SETSYM_OR_FAIL(director.real.CRYPTO_get_locking_callback, "CRYPTO_get_locking_callback");
		return director.real.CRYPTO_get_locking_callback();
	}
}

void* CRYPTO_get_id_callback() {
	if(shouldRedirect()) {
		return director.shadow.CRYPTO_get_id_callback();
	} else {
		if(!director.real.CRYPTO_get_id_callback)
			SETSYM_OR_FAIL(director.real.CRYPTO_get_id_callback, "CRYPTO_get_id_callback");
		return director.real.CRYPTO_get_id_callback();
	}
}

void RAND_seed(const void *buf, int num) {
	if(shouldRedirect()) {
		director.shadow.RAND_seed(buf, num);
	} else {
		if(!director.real.RAND_seed)
			SETSYM_OR_FAIL(director.real.RAND_seed, "RAND_seed");
		director.real.RAND_seed(buf, num);
	}
}

void RAND_add(const void *buf, int num, double entropy) {
	if(shouldRedirect()) {
		director.shadow.RAND_add(buf, num, entropy);
	} else {
		if(!director.real.RAND_add)
			SETSYM_OR_FAIL(director.real.RAND_add, "RAND_add");
		director.real.RAND_add(buf, num, entropy);
	}
}

int RAND_poll() {
	if(shouldRedirect()) {
		return director.shadow.RAND_poll();
	} else {
		if(!director.real.RAND_poll)
			SETSYM_OR_FAIL(director.real.RAND_poll, "RAND_poll");
		return director.real.RAND_poll();
	}
}

int RAND_bytes(unsigned char *buf, int num) {
	if(shouldRedirect()) {
		return director.shadow.RAND_bytes(buf, num);
	} else {
		if(!director.real.RAND_bytes)
			SETSYM_OR_FAIL(director.real.RAND_bytes, "RAND_bytes");
		return director.real.RAND_bytes(buf, num);
	}
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
	if(shouldRedirect()) {
		return director.shadow.RAND_pseudo_bytes(buf, num);
	} else {
		if(!director.real.RAND_pseudo_bytes)
			SETSYM_OR_FAIL(director.real.RAND_pseudo_bytes, "RAND_pseudo_bytes");
		return director.real.RAND_pseudo_bytes(buf, num);
	}
}

void RAND_cleanup() {
	if(shouldRedirect()) {
		director.shadow.RAND_cleanup();
	} else {
		if(!director.real.RAND_cleanup)
			SETSYM_OR_FAIL(director.real.RAND_cleanup, "RAND_cleanup");
		director.real.RAND_cleanup();
	}
}

int RAND_status() {
	if(shouldRedirect()) {
		return director.shadow.RAND_status();
	} else {
		if(!director.real.RAND_status)
			SETSYM_OR_FAIL(director.real.RAND_status, "RAND_status");
		return director.real.RAND_status();
	}
}

const void *RAND_get_rand_method() {
	if(shouldRedirect()) {
		return director.shadow.RAND_get_rand_method();
	} else {
		if(!director.real.RAND_get_rand_method)
			SETSYM_OR_FAIL(director.real.RAND_get_rand_method, "RAND_get_rand_method");
		return director.real.RAND_get_rand_method();
	}
}
