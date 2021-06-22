#ifndef SRC_SHIM_SHIM_SYSCALL_H_
#define SRC_SHIM_SHIM_SYSCALL_H_

#include <stdarg.h>
#include <stdbool.h>

/// This module allows us to short-circuit syscalls that can be handled directly
/// in the shim without needing to perform a more expensive inter-pocess syscall
/// operation with shadow.

// Caches the current simulation time to avoid invoking syscalls to get it.
// Not thread safe, but doesn't matter since Shadow only permits
// one thread at a time to run anyway.
void shim_syscall_set_simtime_nanos(uint64_t simulation_nanos);

// Returns the current cached simulation time, or 0 if it has not yet been set.
uint64_t shim_syscall_get_simtime_nanos();

// Attempt to service a syscall using shared memory if available.
//
// Returns true on success, meaning we indeed handled the syscall.
// Returns false on failure, meaning we do not have the necessary information to
// properly handle the syscall.
//
// If this function returns true, then the raw syscall result is returned
// through `rv`.  e.g. for a syscall returning an error, it's the caller's
// responsibility to set errno from `rv`.
bool shim_syscall(long syscall_num, long* rv, va_list args);

#endif // SRC_SHIM_SHIM_SYSCALL_H_