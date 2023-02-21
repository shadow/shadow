/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_CUSTOM_H_
#define SRC_MAIN_HOST_SYSCALL_CUSTOM_H_

#include "main/host/syscall/protected.h"

// Handle the custom shadow-specific syscalls defined in syscall_numbers.h
SYSCALL_HANDLER(shadow_get_shm_blk);
SYSCALL_HANDLER(shadow_hostname_to_addr_ipv4);
SYSCALL_HANDLER(shadow_init_memory_manager);
SYSCALL_HANDLER(shadow_yield);

#endif /* SRC_MAIN_HOST_SYSCALL_CUSTOM_H_ */
