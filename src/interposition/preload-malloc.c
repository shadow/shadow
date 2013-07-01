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

typedef int (*WorkerIsInShadowContextFunc)();

typedef void* (*MallocFunc)(size_t);
typedef void* (*CallocFunc)(size_t, size_t);
typedef void* (*ReallocFunc)(void*, size_t);
typedef int (*PosixMemalignFunc)(void**, size_t, size_t);
typedef void* (*MemalignFunc)(size_t, size_t);
typedef void* (*AlignedAllocFunc)(size_t, size_t);
typedef void* (*VallocFunc)(size_t);
typedef void* (*PvallocFunc)(size_t);
typedef void (*FreeFunc)(void*);

#define SETSYM_OR_FAIL(funcptr, funcstr) { \
	dlerror(); \
	funcptr = dlsym(RTLD_NEXT, funcstr); \
	char* errorMessage = dlerror(); \
	if(errorMessage != NULL) { \
		fprintf(stderr, "%s\n", errorMessage); \
		exit(EXIT_FAILURE); \
	} else if(funcptr == NULL) { \
		fprintf(stderr, "%s\n", "NULL pointer after dlerror"); \
		exit(EXIT_FAILURE); \
	} \
}

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
} MemoryFuncs;

typedef struct {
	struct {
		char buf[10240];
		unsigned long pos;
		unsigned long nallocs;
		unsigned long ndeallocs;
	} dummy;
	MemoryFuncs real;
	MemoryFuncs shadow;
	WorkerIsInShadowContextFunc isShadowFunc;
	unsigned long isRecursive;
} FuncDirector;

__thread FuncDirector director;

static inline int shouldRedirect() {
	int doRedirect = 0;
	/* recursive calls always go to the syscall */
	int isRecursive = __sync_fetch_and_add(&director.isRecursive, 1);
	if(!isRecursive) {
		/* ask shadow if this call is a plug-in that should be intercepted */
		doRedirect = director.isShadowFunc() ? 0 : 1;
	}
	__sync_fetch_and_sub(&director.isRecursive, 1);
	return doRedirect;
}

static void* dummy_malloc(size_t size) {
    if (director.dummy.pos + size >= sizeof(director.dummy.buf)) exit(1);
    void *retptr = director.dummy.buf + director.dummy.pos;
    director.dummy.pos += size;
    ++director.dummy.nallocs;
    return retptr;
}

static void* dummy_calloc(size_t nmemb, size_t size) {
    void *ptr = dummy_malloc(nmemb * size);
    unsigned int i = 0;
    for (; i < nmemb * size; ++i)
        *((char*)(ptr + i)) = '\0';
    return ptr;
}

static void dummy_free(void *ptr) {
	++director.dummy.ndeallocs;
	if(director.dummy.ndeallocs == director.dummy.nallocs){
		director.dummy.pos = 0;
	}
}

void __attribute__((constructor)) InitMemoryFuncs() {
	/* use dummy malloc during initial dlsym calls to avoid recursive stack segfaults */
	memset(&director, 0, sizeof(FuncDirector));
	director.real.malloc = dummy_malloc;
	director.real.calloc = dummy_calloc;
	director.real.free = dummy_free;

	MemoryFuncs temp;
	memset(&temp, 0, sizeof(MemoryFuncs));

	/* ensure we never intercept during initialization */
	__sync_fetch_and_add(&director.isRecursive, 1);

	SETSYM_OR_FAIL(temp.malloc, "malloc");
	SETSYM_OR_FAIL(temp.calloc, "calloc");
	SETSYM_OR_FAIL(temp.realloc, "realloc");
	SETSYM_OR_FAIL(temp.free, "free");
	SETSYM_OR_FAIL(temp.posix_memalign, "posix_memalign");
	SETSYM_OR_FAIL(temp.memalign, "memalign");
	SETSYM_OR_FAIL(temp.aligned_alloc, "aligned_alloc");
	SETSYM_OR_FAIL(temp.valloc, "valloc");
	SETSYM_OR_FAIL(temp.pvalloc, "pvalloc");

	SETSYM_OR_FAIL(director.shadow.malloc, "intercept_malloc");
	SETSYM_OR_FAIL(director.shadow.calloc, "intercept_calloc");
	SETSYM_OR_FAIL(director.shadow.realloc, "intercept_realloc");
	SETSYM_OR_FAIL(director.shadow.free, "intercept_free");
	SETSYM_OR_FAIL(director.shadow.posix_memalign, "intercept_posix_memalign");
	SETSYM_OR_FAIL(director.shadow.memalign, "intercept_memalign");
	SETSYM_OR_FAIL(director.shadow.aligned_alloc, "intercept_aligned_alloc");
	SETSYM_OR_FAIL(director.shadow.valloc, "intercept_valloc");
	SETSYM_OR_FAIL(director.shadow.pvalloc, "intercept_pvalloc");

	SETSYM_OR_FAIL(director.isShadowFunc, "intercept_worker_isInShadowContext");

	/* stop using dummy funcs now */
    director.real.malloc = temp.malloc;
    director.real.calloc = temp.calloc;
    director.real.realloc = temp.realloc;
    director.real.free = temp.free;
    director.real.posix_memalign = temp.posix_memalign;
    director.real.memalign = temp.memalign;
    director.real.aligned_alloc = temp.aligned_alloc;
    director.real.valloc = temp.valloc;
    director.real.pvalloc = temp.pvalloc;

    __sync_fetch_and_sub(&director.isRecursive, 1);
}

void* malloc(size_t size) {
    return shouldRedirect() ? director.shadow.malloc(size) : director.real.malloc(size);
}

void* calloc(size_t nmemb, size_t size) {
    return shouldRedirect() ? director.shadow.calloc(nmemb, size) : director.real.calloc(nmemb, size);
}

void* realloc(void *ptr, size_t size) {
    return shouldRedirect() ? director.shadow.realloc(ptr, size) : director.real.realloc(ptr, size);
}

void free(void *ptr) {
    shouldRedirect() ? director.shadow.free(ptr) : director.real.free(ptr);
}

int posix_memalign(void** memptr, size_t alignment, size_t size) {
    return shouldRedirect() ? director.shadow.posix_memalign(memptr, alignment, size) : director.real.posix_memalign(memptr, alignment, size);
}

void* memalign(size_t blocksize, size_t bytes) {
    return shouldRedirect() ? director.shadow.memalign(blocksize, bytes) : director.real.memalign(blocksize, bytes);
}

void* aligned_alloc(size_t alignment, size_t size) {
	return shouldRedirect() ? director.shadow.aligned_alloc(alignment, size) : director.real.aligned_alloc(alignment, size);
}

void* valloc(size_t size) {
    return shouldRedirect() ? director.shadow.valloc(size) : director.real.valloc(size);
}

void* pvalloc(size_t size) {
	return shouldRedirect() ? director.shadow.pvalloc(size) : director.real.pvalloc(size);
}
