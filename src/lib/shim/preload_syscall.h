#ifndef PRELOAD_SYSCALL_H
#define PRELOAD_SYSCALL_H

#include <stdarg.h>

// The function the shim uses to execute a bare syscall instruction.
// Similar to libc's `syscall`, but *doesn't* remap return values to errno.
long __attribute__((noinline)) shadow_vreal_raw_syscall(long n, va_list args);

// Make a raw syscall (without remapping return val to errno). Internally
// decides whether to execute a real syscall or emulate.
long shadow_raw_syscall(long n, ...);

// Makes a raw syscall natively; never emulates.
long shadow_real_raw_syscall(long n, ...);

#endif