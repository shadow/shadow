/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shd-preload-includes.h"

//typedef void* (*malloc_func)(size_t);
//typedef void* (*calloc_func)(size_t, size_t);
//typedef void (*free_func)(void*);
PRELOADDEF(return, void*, malloc, (size_t a), a);
PRELOADDEF(return, void*, calloc, (size_t a, size_t b), a, b);
PRELOADDEF(      , void, free, (void* a), a);

//typedef int (*fcntl_func)(int, int, ...);
//typedef int (*ioctl_func)(int, int, ...);
PRELOADDEF(return, int, ioctl, (int a, int b, ...), a, b);
PRELOADDEF(return, int, fcntl, (int a, int b, ...), a, b);

//typedef int (*printf_func)(const char *, ...);
//typedef int (*fprintf_func)(FILE *, const char *, ...);
PRELOADDEF(return, int, printf, (const char* a, ...), a);
PRELOADDEF(return, int, fprintf, (FILE* a, const char* b, ...), a, b);

//typedef int (*open_func)(const char*, int, mode_t);
//typedef int (*open64_func)(const char*, int, mode_t);
//typedef int (*openat_func)(int, const char*, int, mode_t);
PRELOADDEF(return, int, open, (const char* a, int b, mode_t c), a, b, c);
PRELOADDEF(return, int, open64, (const char* a, int b, mode_t c), a, b, c);
PRELOADDEF(return, int, openat, (int a, const char* b, int c, mode_t d), a, b, c, d);

//typedef void (*pthread_exit_func)(void *);
//typedef void (*exit_func)(int status);
PRELOADDEF(      , void, pthread_exit, (void* a), a);
PRELOADDEF(      , void, exit, (int a), a);
PRELOADDEF(      , void, abort, (void));

PRELOADDEF(return, int, syscall, (int a, ...), a);

/* intercepting these functions causes glib errors, because keys that were created from
 * internal shadow functions then get used in the plugin and get forwarded to pth, which
 * of course does not have the same registered keys. */
//INTERPOSE(int pthread_key_create(pthread_key_t *a, void (*b)(void *)), pthread_key_create, a, b);
//INTERPOSE(int pthread_key_delete(pthread_key_t a), pthread_key_delete, a);
//INTERPOSE(int pthread_setspecific(pthread_key_t a, const void *b), pthread_setspecific, a, b);
//INTERPOSE(void* pthread_getspecific(pthread_key_t a), pthread_getspecific, a);

/* these cause undocumented errors */
//INTERPOSE_NORET(void pthread_cleanup_push(void (*a)(void *), void *b), pthread_cleanup_push, a, b);
//INTERPOSE_NORET(void pthread_cleanup_pop(int a), pthread_cleanup_pop, a, b);

/* BLEEP related functions*/
// BLEEP library support
PRELOADDEF(return, int, puts_temp, (const char *a), a);
PRELOADDEF(return, int, shadow_pipe2, (int a[2], int b), a, b);
PRELOADDEF(return, int, shadow_push_eventlog, (const char *a), a);
PRELOADDEF(return, int, shadow_usleep, (unsigned int a), a);
PRELOADDEF(return, int, shadow_clock_gettime, (clockid_t a, struct timespec *b), a, b);

// BLEEP attacker support
PRELOADDEF(return, int, shadow_bind, (int fd, const struct sockaddr* addr, socklen_t len), fd, addr, len);

// BLEEP Shared Entry Functions
PRELOADDEF(return, void*, shadow_claim_shared_entry, (void* ptr, size_t sz, int shared_id), ptr, sz, shared_id);
PRELOADDEF(return, void, shadow_gmutex_lock, (int shared_id), shared_id);
PRELOADDEF(return, void, shadow_gmutex_unlock, (int shared_id), shared_id);
// BLEEP Virtual ID Functions
PRELOADDEF(return, int, shadow_assign_virtual_id, (void));
// BLEEP TCP PTR send/recv Functions

// Memory Instrumentation Marker Functions
PRELOADDEF(return, void, shadow_instrumentation_marker_set, (int file_symbol, int line_cnt), file_symbol, line_cnt);
PRELOADDEF(return, void, hj_interposer_test, (void));