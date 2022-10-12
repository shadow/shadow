#ifndef PRELOAD_SYSCALL_H
#define PRELOAD_SYSCALL_H

#include <stdarg.h>
#include <stdbool.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"

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

#endif