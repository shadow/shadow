/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "preload_includes.h"

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
PRELOADDEF(      , void, __pthread_unwind_next, (__pthread_unwind_buf_t *a), a);
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
