/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_POLL_H_
#define SRC_MAIN_HOST_SYSCALL_POLL_H_

#include <poll.h>

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(poll);
SYSCALL_HANDLER(ppoll);

/* Protected helper to allow select() to redirect here. */
SyscallReturn _syscallhandler_pollHelper(SyscallHandler* sys, struct pollfd* fds, nfds_t nfds,
                                         const struct timespec* timeout);

#endif /* SRC_MAIN_HOST_SYSCALL_POLL_H_ */
