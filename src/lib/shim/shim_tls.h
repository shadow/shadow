#ifndef SHIM_TLS_H_
#define SHIM_TLS_H_

// Bare bones implementation of thread-local-storage. Never makes syscalls.
//
// This is useful for some core functionality of the shim, such as tracking the
// per-thread IPC block, whether interposition is enabled, etc. Using native
// thread-local-storage for these can be problematic because the implementation
// can make syscalls, leading to infinite recursion.

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

// Global init. Must be called before accessing any ShimTlsVars.
// If `useNativeTls` is set to false, then there's no need to do any of the
// manual TlsIdx management below.
void shimtls_init(bool useNativeTls);

// A thread-local variable.
//
// Instances should have static storage type, and be zero-initialized. e.g.:
//
//     static ShimTlsVar v = {0};
//
// To get a pointer to the current thread's instance of the variable, use
// `shimtlsvar_ptr`:
//
//     MyType* t = shimtlsvar_ptr(&v, sizeof(*t));
//
typedef struct ShimTlsVar {
    size_t _offset;
    bool _initd;
} ShimTlsVar;

// Return pointer to this thread's instance of the given var. The pointer is
// always align(16), and the data at that pointer is zero-initialized for each
// thread.
void* shimtlsvar_ptr(ShimTlsVar* v, size_t sz);

// Take an unused TLS index, which can be used for a new thread.
int shimtls_takeNextIdx();

// Use when switching threads. Must be called before accessing any ShimTlsVars
// in each new thread, or after switching threads.
//
// This has no effect if `useNativeTls` was set to true.
void shimtls_setCurrentIdx(int idx);

// Get the current thread idx, which should be treated as opaque. Always returns 0
// if `useNativeTls` was set to true.
int shimtls_getCurrentIdx();

#endif