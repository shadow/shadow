/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_FUTEX_H_
#define SRC_MAIN_HOST_SYSCALL_FUTEX_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(futex);
SYSCALL_HANDLER(get_robust_list);
SYSCALL_HANDLER(set_robust_list);

#endif /* SRC_MAIN_HOST_SYSCALL_FUTEX_H_ */
