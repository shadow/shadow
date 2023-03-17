#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

#include "lib/shadow-shim-helper-rs/shim_helper.h"

// Handle SHIM_EVENT_ID_CLONE_REQ
void shim_shmemHandleClone(const ShimEvent* ev);

// Handle SHIM_EVENT_ID_CLONE_STRING_REQ
void shim_shmemHandleCloneString(const ShimEvent* ev);

// Handle SHIM_EVENT_ID_WRITE_REQ
void shim_shmemHandleWrite(const ShimEvent* ev);

// Notify Shadow that a shared memory event has been handled.
void shim_shmemNotifyComplete(struct IPCData* data);

#endif // SHD_SHIM_SHMEM_H_
