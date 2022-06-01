#ifndef SHIM_TLS_H_
#define SHIM_TLS_H_

// Bare bones implementation of thread-local-storage.
//
// The shim relies on thread-local-storage to track data such as the per-thread
// IPC block, whether interposition is enabled, etc. However, many of the
// implementation details of "native" thread local storage are unspecified. e.g.
// in CentOS 7, the first access to thread-local-storage in a child thread
// lazily set up the TLS, which makes syscalls, resulting in an infinite loop.
//
// While the current implementation of this library just uses native thread
// local storage (__thread), using this library within the shim instead of
// directly using __thread gives us the option to avoid depending on the
// implementation details of __thread, or even permit `clone` calls that don't
// set up native thread local storage at all. For example, we could inspect the
// current stack pointer or (if necessary) make a gettid syscall to determine
// which thread we're running in.

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

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

#endif