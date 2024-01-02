/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_UNISTD_H_
#define SRC_MAIN_HOST_SYSCALL_UNISTD_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(pread64);
SYSCALL_HANDLER(pwrite64);
SYSCALL_HANDLER(read);
SYSCALL_HANDLER(write);

SyscallReturn _syscallhandler_readHelper(SyscallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                         size_t bufSize, off_t offset, bool doPread);

SyscallReturn _syscallhandler_writeHelper(SyscallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                          size_t bufSize, off_t offset, bool doPwrite);

#endif /* SRC_MAIN_HOST_SYSCALL_UNISTD_H_ */
