#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

#include "shim_event.h"
#include "spin.h"

// Handle SHD_SHIM_EVENT_CLONE_REQ
void shim_shmemHandleClone(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_CLONE_STRING_REQ
void shim_shmemHandleCloneString(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_WRITE_REQ
void shim_shmemHandleWrite(const ShimEvent* ev);

// Notify Shadow that a shared memory event has been handled.
void shim_shmemNotifyComplete(struct IPCData *data);

#endif // SHD_SHIM_SHMEM_H_
