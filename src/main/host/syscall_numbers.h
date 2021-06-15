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
    SYS_shadow_get_ipc_blk = 1001,
    SYS_shadow_get_shm_blk = 1002,
    SYS_shadow_hostname_to_addr_ipv4 = 1003,
    SYS_shadow_max = 1003,
} ShadowSyscallNum;

static inline bool syscall_num_is_shadow(long n) {
    return n >= SYS_shadow_min && n <= SYS_shadow_max;
};

#endif /* SRC_MAIN_HOST_SYSCALL_NUMBERS_H_ */