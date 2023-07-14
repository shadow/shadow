/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_NUMBERS_H_
#define SRC_MAIN_HOST_SYSCALL_NUMBERS_H_

#include <stdbool.h>

typedef enum {
    SYS_shadow_min = 1000,
    SYS_shadow_set_ptrace_allow_native_syscalls = 1000,
    // Deprecated: SYS_shadow_get_ipc_blk = 1001,
    // Deprecated: SYS_shadow_get_shm_blk = 1002,
    SYS_shadow_hostname_to_addr_ipv4 = 1003,
    SYS_shadow_init_memory_manager = 1004,
    // Conceptually similar to SYS_sched_yield, but made by the shim to return
    // control to Shadow. For now, using a different syscall here is mostly for
    // debugging purposes, so that it doesn't appear that the managed code
    // issues a SYS_sched_yield.
    SYS_shadow_yield = 1005,
    SYS_shadow_max = 1005,
} ShadowSyscallNum;

static inline bool syscall_num_is_shadow(long n) {
    return n >= SYS_shadow_min && n <= SYS_shadow_max;
};

#endif /* SRC_MAIN_HOST_SYSCALL_NUMBERS_H_ */