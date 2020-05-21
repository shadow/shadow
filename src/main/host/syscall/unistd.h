/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_UNISTD_H_
#define SRC_MAIN_HOST_SYSCALL_UNISTD_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(read);
SYSCALL_HANDLER(write);
SYSCALL_HANDLER(close);
SYSCALL_HANDLER(pipe);
SYSCALL_HANDLER(pipe2);
SYSCALL_HANDLER(getpid);
SYSCALL_HANDLER(uname);

#endif /* SRC_MAIN_HOST_SYSCALL_UNISTD_H_ */
