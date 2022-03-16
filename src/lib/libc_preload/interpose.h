/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>

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

// As above, but return positive error values instead of -1 + errno.
// Note that errno still gets mutated, but this is permitted according to errno(3).
#define INTERPOSE_REMAP_DIRECT_ERRORS(func_name, syscall_name)                                     \
    long func_name(long a, long b, long c, long d, long e, long f) {                               \
        long rv = syscall(SYS_##syscall_name, a, b, c, d, e, f);                                   \
        if (rv == -1) {                                                                            \
            rv = errno;                                                                            \
        }                                                                                          \
        return rv;                                                                                 \
    }

// Defines a thin wrapper whose function name 'func_name' is the same as the syscall name.
#define INTERPOSE(func_name) INTERPOSE_REMAP(func_name, func_name)
#define INTERPOSE_DIRECT_ERRORS(func_name) INTERPOSE_REMAP_DIRECT_ERRORS(func_name, func_name)