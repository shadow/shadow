/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_FILEAT_H_
#define SRC_MAIN_HOST_SYSCALL_FILEAT_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(faccessat);
SYSCALL_HANDLER(fchmodat);
SYSCALL_HANDLER(fchmodat2);
SYSCALL_HANDLER(fchownat);
SYSCALL_HANDLER(futimesat);
SYSCALL_HANDLER(linkat);
SYSCALL_HANDLER(mkdirat);
SYSCALL_HANDLER(mknodat);
SYSCALL_HANDLER(newfstatat);
SYSCALL_HANDLER(openat);
SYSCALL_HANDLER(readlinkat);
SYSCALL_HANDLER(renameat);
SYSCALL_HANDLER(renameat2);
SYSCALL_HANDLER(statx);
SYSCALL_HANDLER(symlinkat);
SYSCALL_HANDLER(unlinkat);
SYSCALL_HANDLER(utimensat);

#endif /* SRC_MAIN_HOST_SYSCALL_FILEAT_H_ */
