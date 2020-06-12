/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_UNISTD_H_
#define SRC_MAIN_HOST_SYSCALL_UNISTD_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(close);
SYSCALL_HANDLER(getpid);
SYSCALL_HANDLER(pipe);
SYSCALL_HANDLER(pipe2);
SYSCALL_HANDLER(pread64);
SYSCALL_HANDLER(pwrite64);
SYSCALL_HANDLER(read);
SYSCALL_HANDLER(uname);
SYSCALL_HANDLER(write);

#endif /* SRC_MAIN_HOST_SYSCALL_UNISTD_H_ */
