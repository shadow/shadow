#ifndef PRELOAD_SYSCALL_H
#define PRELOAD_SYSCALL_H

#include <stdarg.h>
#include <stdbool.h>

#include "lib/shim/shim_shmem.h"

// Ask the shim to handle a syscall. Internally decides whether to execute a
// native syscall or to emulate the syscall through Shadow.
long shim_syscall(long n, ...);

// Same as `shim_syscall()`, but accepts a variable argument list.
long shim_syscallv(long n, va_list args);

// Force the native execution of a syscall instruction (using asm so it can't be
// intercepted).
long shim_native_syscall(long n, ...);

// Same as `shim_native_syscall()`, but accepts a variable argument list.
// We disable inlining so seccomp can allow syscalls made from this function.
long __attribute__((noinline)) shim_native_syscallv(long n, va_list args);

// Force the emulation of the syscall through Shadow.
long shim_emulated_syscall(long n, ...);

// Same as `shim_emulated_syscall()`, but accepts a variable argument list.
long shim_emulated_syscallv(long n, va_list args);

// Handle pending unblocked signals, and return whether *all* corresponding
// signal actions had the SA_RESTART flag set.
//
// `ucontext` will be passed through to handlers if non-NULL. This should
// generally only be done if the caller has a `ucontext` that will be swapped to
// after this code returns; e.g. one that was passed to our own signal handler,
// which will be swapped to when that handler returns.
//
// If `ucontext` is NULL, one will be created at the point where we invoke
// the handler, and swapped back to when it returns.
// TODO: Creating `ucontext_t` is currently only implemented for handlers that
// execute on a sigaltstack.
bool shim_process_signals(ShimShmemHostLock* host_lock, ucontext_t* ucontext);

#endif