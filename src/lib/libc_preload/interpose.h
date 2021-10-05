/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

// To get the SYS_xxx defs. Do not include other headers in the same file where
// interpose.h is included in order to avoid conflicts.
#include <sys/syscall.h>

// External declaration, to minimize the headers we need to include.
long syscall(long n, ...);

// Defines a thin wrapper function `func_name` that invokes the syscall `syscall_name`.
#define INTERPOSE_REMAP(func_name, syscall_name)                                                   \
    long func_name(long a, long b, long c, long d, long e, long f) {                               \
        return syscall(SYS_##syscall_name, a, b, c, d, e, f);                                      \
    }

// Defines a thin wrapper whose function name 'func_name' is the same as the syscall name.
#define INTERPOSE(func_name) INTERPOSE_REMAP(func_name, func_name)
