#ifndef SHIM_SHADOW_SEM_H
#define SHIM_SHADOW_SEM_H

// Implements the same API as sem_init, sem_destroy, etc. from libc.
//
// This is a shared implementation used both in binary_spinning_sem and in
// preload_libraries.

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
// Unfortunately C++ and C don't have a mutually compatible atomic type.  These
// fields shold be treated as opaque anyway though; C++ code just needs to know
// the size and alignment.
#define _Atomic(x) x
#else
#include <stdatomic.h>
#endif

typedef struct shadow_sem_t {
    _Atomic(uint32_t) _value;
    _Atomic(uint32_t) _nwaiters;
} shadow_sem_t;

// Validate size and alignment since we lied to C++
static_assert(sizeof(shadow_sem_t) == 8, "shadow_sem_t has unexpected size");
static_assert(alignof(shadow_sem_t) == 4, "shadow_sem_t has unexpected alignment");

int shadow_sem_init(shadow_sem_t* sem, int pshared, unsigned int value);
int shadow_sem_destroy(shadow_sem_t* sem);
int shadow_sem_post(shadow_sem_t* sem);
int shadow_sem_trywait(shadow_sem_t* sem);
int shadow_sem_timedwait(shadow_sem_t* sem, const struct timespec* abs_timeout);
int shadow_sem_wait(shadow_sem_t* sem);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif