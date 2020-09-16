#ifndef SRC_MAIN_HOST_SYSCALL_CLONE_H_
#define SRC_MAIN_HOST_SYSCALL_CLONE_H_

/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#include "main/host/syscall/protected.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"

SYSCALL_HANDLER(clone);
SYSCALL_HANDLER(gettid);

#endif /* SRC_MAIN_HOST_SYSCALL_CLONE_H_ */
