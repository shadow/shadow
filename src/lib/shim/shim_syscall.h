#ifndef PRELOAD_SYSCALL_H
#define PRELOAD_SYSCALL_H

#include <stdarg.h>
#include <stdbool.h>
#include <sys/ucontext.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"

// Ask the shim to handle a syscall. Internally decides whether to execute a
// native syscall or to emulate the syscall through Shadow.
long shim_syscall(ucontext_t* ctx, long n, ...);

// Same as `shim_syscall()`, but accepts a variable argument list.
long shim_syscallv(ucontext_t* ctx, long n, va_list args);

// Force the native execution of a syscall instruction (using asm so it can't be
// intercepted).
long shim_native_syscall(ucontext_t* ctx, long n, ...);

// Force the emulation of the syscall through Shadow.
long shim_emulated_syscall(ucontext_t* ctx, long n, ...);

#endif
