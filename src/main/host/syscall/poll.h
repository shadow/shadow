/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_POLL_H_
#define SRC_MAIN_HOST_SYSCALL_POLL_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(poll);
SYSCALL_HANDLER(ppoll);

#endif /* SRC_MAIN_HOST_SYSCALL_POLL_H_ */
