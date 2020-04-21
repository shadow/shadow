/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <stdarg.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#if defined __USE_MISC
#undef __USE_MISC
#define __USE_MISC 1
#else
#include <unistd.h>
#endif
#include <features.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include "main/core/worker.h"
#include "main/core/support/definitions.h"
#include "main/host/process.h"
#include "preload/preload_functions.h"

#define SETSYM_OR_FAIL(funcptr, funcstr) { \
    dlerror(); \
    funcptr = dlsym(RTLD_NEXT, funcstr);\
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

// We use this to convince the compiler that functions declared noreturn really
// don't return... also to fail in some well defined way instead of getting
// undefined behavior if we're wrong.
noreturn static void ensure_noreturn() {
    ENSURE(abort);
    director.next.abort();
    // Because this function is declared noreturn, returning would result in
    // undefined behavior.
    // Recurse until stack overflow instead.
    ensure_noreturn();
}

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

int interposer_setShadowIsLoaded(int isLoaded) {
    director.shadowIsLoaded = isLoaded;
    return 0;
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
    preload_functions_do_lookups(&(director.next), RTLD_NEXT);

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

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
#define PRELOADDEF(returnstatement, returntype, functionname, argumentlist, ...) \
returntype functionname argumentlist { \
    Process* proc = NULL; \
    if((proc = _doEmulate()) != NULL) { \
        returnstatement process_emu_##functionname(proc, ##__VA_ARGS__); \
    } else { \
        ENSURE(functionname); \
        returnstatement director.next.functionname(__VA_ARGS__); \
    } \
}

#include "preload/preload_defs.h"

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif


/* functions that must be handled without macro */

void* malloc(size_t size) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        return process_emu_malloc(proc, size);
    } else {
        /* the dlsym lookup for calloc may call calloc again, causing infinite recursion */
        if(!director.next.malloc) {
            /* make sure to use dummy_calloc when looking up calloc */
            director.next.malloc = dummy_malloc;
            /* this will set director.real.calloc to the correct calloc */
            ENSURE(malloc);
        }
        return director.next.malloc(size);
    }
}

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

/* syscall */

long syscall(long number, ...) {
    va_list arglist;
    va_start(arglist, number);
    int result = 0;
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        result = process_emu_syscall(proc, number, arglist);
    } else {
        ENSURE(syscall);
        result = director.next.syscall(number, arglist);
    }
    va_end(arglist);
    return result;
}

/* exit family */

void exit(int a) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        process_emu_exit(proc, a);
    } else {
        ENSURE(exit);
        director.next.exit(a);
    }
    ensure_noreturn();
}

void pthread_exit(void* a) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        process_emu_pthread_exit(proc, a);
    } else {
        ENSURE(pthread_exit);
        director.next.pthread_exit(a);
    }
    ensure_noreturn();
}

void __pthread_unwind_next(__pthread_unwind_buf_t* buf) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
	// __pthread_unwind_next shouldn't have been called from emulated code.
	// It's normally only called via other pthread internals, which we
	// should've intercepted at a higher level. Ideally if this somehow
	// happens we'd just kill the thread, but calling pthread_exit would
	// likely result in recursion.  Take down the whole emulated process
	// instead.
        abort();
    } else {
        ENSURE(__pthread_unwind_next);
        director.next.__pthread_unwind_next(buf);
    }
    ensure_noreturn();
}

void abort(void) {
    Process* proc = NULL;
    if((proc = _doEmulate()) != NULL) {
        process_emu_abort(proc);
    } else {
        ENSURE(abort);
        director.next.abort();
    }
    ensure_noreturn();
}
