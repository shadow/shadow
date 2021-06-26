#ifndef SHD_SHIM_SHIM_H_
#define SHD_SHIM_SHIM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "main/shmem/shmem_allocator.h"

// Should be called by all syscall wrappers to ensure the shim is initialized.
void shim_ensure_init();

// Disables syscall interposition for the current thread if it's enabled.
// Every call to this function should be matched with a call to shim_enableInterposition().
// Returns true if interposition was enabled before this call (and so this call caused it
// to become disabled), or false if it was already disabled.
bool shim_disableInterposition();

// Re-enabled syscall interposition for the current thread if it's disabled.
// Every call to this function should be matched with a call to shim_disableInterposition().
// Returns true if interposition was disabled before this call (and so this call caused it
// to become enabled), or false if it was already enabled.
bool shim_enableInterposition();

// Whether syscall interposition is currently enabled.
bool shim_interpositionEnabled();

// Whether we are using the shim-side syscall handler.
bool shim_use_syscall_handler();

// Returns the shmem block used for IPC, which may be uninitialized.
struct IPCData* shim_thisThreadEventIPC();

// Return the location of the time object in shared memory, or NULL if unavailable.
struct timespec* shim_get_shared_time_location();

#endif // SHD_SHIM_SHIM_H_
