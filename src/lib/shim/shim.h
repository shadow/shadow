#ifndef SHD_SHIM_SHIM_H_
#define SHD_SHIM_SHIM_H_

#include <stdatomic.h>
#include <sys/types.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/core/support/definitions.h"

// Should be called by all syscall wrappers to ensure the shim is initialized.
void shim_ensure_init();

// Sets the flag determining whether syscalls are passed through natively, and
// returns the old value. Typical usage is to set this to the desired value at
// the beginning of an operation, and restore the old value afterwards.
bool shim_swapAllowNativeSyscalls(bool new);

// Whether syscall interposition is currently enabled.
bool shim_interpositionEnabled();

// Returns the shmem block used for IPC, which may be uninitialized.
struct IPCData* shim_thisThreadEventIPC();

// To be called in parent thread before making the `clone` syscall.
// It sets up data for the new thread.
void shim_newThreadStart(const ShMemBlockSerialized* block);

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
// Shouldn't need to make this very large, but needs to be big enough to run the
// managed process's signal handlers as well - possibly recursively.
//
// Stack space that's *never* used shouldn't ever become resident, but an
// occasional deep stack could force the pages to be resident ever after.  To
// mitigate that, we could consider `madvise(MADV_DONTNEED)` after running
// signal handlers, to let the OS reclaim the (now-popped) signal handler stack
// frames.
#define SHIM_SIGNAL_STACK_MIN_USABLE_SIZE ((size_t)1024 * 100)
#define SHIM_SIGNAL_STACK_SIZE                                                                     \
    (SHIM_SIGNAL_STACK_GUARD_OVERHEAD + SHIM_SIGNAL_STACK_MIN_USABLE_SIZE)

ShimShmemThread* shim_threadSharedMem();
ShimShmemProcess* shim_processSharedMem();
ShimShmemHost* shim_hostSharedMem();

// Prepare to free the current thread's signal thread stack.  Should only be
// done just before exiting, as the stack will be freed the next another thread
// exits.
void shim_freeSignalStack();

#endif // SHD_SHIM_SHIM_H_
