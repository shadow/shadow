#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

/*
 * Event loop to handle shared-memory IPC after a syscall is made.
 */
void shim_shmemLoop(int fd);

#endif // SHD_SHIM_SHMEM_H_
