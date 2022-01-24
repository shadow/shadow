#ifndef SHD_SHIM_SHIM_H_
#define SHD_SHIM_SHIM_H_

#include <stdatomic.h>
#include <sys/types.h>

#include "main/core/support/definitions.h"
#include "main/shmem/shmem_allocator.h"

// Should be called by all syscall wrappers to ensure the shim is initialized.
void shim_ensure_init();

// Sets the flag determining whether syscalls are passed through natively, and
// returns the old value. Typical usage is to set this to the desired value at
// the beginning of an operation, and restore the old value afterwards.
bool shim_swapAllowNativeSyscalls(bool new);

// Whether syscall interposition is currently enabled.
bool shim_interpositionEnabled();

// Whether we are using the shim-side syscall handler.
bool shim_use_syscall_handler();

// Returns the shmem block used for IPC, which may be uninitialized.
struct IPCData* shim_thisThreadEventIPC();

// Return the location of the time object in shared memory, or NULL if unavailable.
_Atomic EmulatedTime* shim_get_shared_time_location();

// To be called in parent thread before making the `clone` syscall.
// It sets up data for the new thread.
void shim_newThreadStart(ShMemBlockSerialized* block);

// To be called in parent thread after making the `clone` syscall.
// It doesn't return until after the child has initialized itself.
void shim_newThreadFinish();

// To be called from a new *child* thread after clone, to notify
// the parent thread that it is now initialized.
void shim_newThreadChildInitd();

// Signal stack size parameters defined here because this is a significant
// portion of the memory that needs to be statically allocated in shim_tls.c.
//
// We use a page for a stack guard, and up to another page to page-align the
// stack guard. We assume 4k pages here but detect at runtime if this is too small.
#define SHIM_SIGNAL_STACK_GUARD_OVERHEAD ((size_t)4096 * 2)
// Found experimentally. 4k seems to be enough for most platforms, but fails on Ubuntu 18.04.
#define SHIM_SIGNAL_STACK_MIN_USABLE_SIZE ((size_t)1024 * 12)
#define SHIM_SIGNAL_STACK_SIZE                                                                     \
    (SHIM_SIGNAL_STACK_GUARD_OVERHEAD + SHIM_SIGNAL_STACK_MIN_USABLE_SIZE)

#endif // SHD_SHIM_SHIM_H_
