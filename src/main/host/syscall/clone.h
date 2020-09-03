/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_CLONE_H_
#define SRC_MAIN_HOST_SYSCALL_CLONE_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(clone);
SYSCALL_HANDLER(gettid);

#endif /* SRC_MAIN_HOST_SYSCALL_CLONE_H_ */
