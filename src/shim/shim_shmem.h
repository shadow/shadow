#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

#include "shim_event.h"

/*
 * Handle a single shared-memory event.
 */
void shim_shmemHandleEvent(int fd, const ShimEvent* ev);

#endif // SHD_SHIM_SHMEM_H_
