#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#include "ipc.h"
#include "main/core/support/definitions.h"
#include "shim_event.h"

// Shared state between Shadow and a plugin-thread.
//
// While synchronized via Shadow/Plugin IPC and/or ptrace-stops, there could be
// potential parallel access e.g. in preload mode's spin lock.  Therefore we
// ensure members are Sync (in Rust parlance) via atomics, and maybe in the
// future mutexes etc.
//
// Atomic members can mostly be dereferenced as normal, though the value should
// be copied locally to ensure a consistent view if accessed multiple times.
// * The compiler will use appropriate atomic-access-instructions when
//   dereferencing.
// * We generally only care about seeing the freshest value when transferring
//   control between Shadow and the Shim, which already includes memory barriers.
//
typedef struct _ShimThreadSharedMem {
    // While true, Shadow allows syscalls to be executed natively.
    atomic_bool ptrace_allow_native_syscalls;
} ShimThreadSharedMem;

// Shared state between Shadow and a plugin-process.
//
// Safety is as for ShimThreadSharedMem, above.
typedef struct _ShimProcessSharedMem {
    // Current simulation time.
    _Atomic EmulatedTime sim_time;
} ShimProcessSharedMem;

// Handle SHD_SHIM_EVENT_CLONE_REQ
void shim_shmemHandleClone(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_CLONE_STRING_REQ
void shim_shmemHandleCloneString(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_WRITE_REQ
void shim_shmemHandleWrite(const ShimEvent* ev);

// Notify Shadow that a shared memory event has been handled.
void shim_shmemNotifyComplete(struct IPCData *data);

#endif // SHD_SHIM_SHMEM_H_
