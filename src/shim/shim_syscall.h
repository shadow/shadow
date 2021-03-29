#ifndef SRC_SHIM_SHIM_SYSCALL_H_
#define SRC_SHIM_SHIM_SYSCALL_H_

#include <stdarg.h>
#include <stdbool.h>

/// This module allows us to short-circuit syscalls that can be handled directly
/// in the shim without neededing to perform a more expensive inter-pocess
/// syscall operation with shadow.

// Caches the current simulation time to avoid invoking syscalls to get it.
// Not thread safe, but doesn't matter since Shadow only permits
// one thread at a time to run anyway.
void shim_syscall_set_simtime_nanos(uint64_t simulation_nanos);

// Returns the current cached simulation time, or 0 if it has not yet been set.
uint64_t shim_syscall_get_simtime_nanos();

// Returns true if the syscall is supported in shimtime_syscall, false otherwise.
// Supported syscalls are clock_gettime(), time(), and gettimeofday().
bool shim_syscall_is_supported(long syscall_num);

// Attempt to service a time-related syscall using a previously-cached
// simulation time or using shared memory if available.
//
// Returns true on success, meaning we indeed handled the syscall.
// Returns false on failure, meaning the simulation time is not
// available from our sources.
//
// If this function returns true, then either:
// - the syscall succeeded, the time is written to args, and rv is set
// - the syscall failed, rv and errno are set
bool shim_syscall(long syscall_num, long* rv, va_list args);

#endif // SRC_SHIM_SHIM_SYSCALL_H_