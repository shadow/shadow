/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_FILE_H_
#define SRC_MAIN_HOST_SYSCALL_FILE_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(creat);
SYSCALL_HANDLER(fadvise64);
SYSCALL_HANDLER(fallocate);
SYSCALL_HANDLER(fchdir);
SYSCALL_HANDLER(fchmod);
SYSCALL_HANDLER(fchown);
SYSCALL_HANDLER(fdatasync);
SYSCALL_HANDLER(fgetxattr);
SYSCALL_HANDLER(flistxattr);
SYSCALL_HANDLER(flock);
SYSCALL_HANDLER(fremovexattr);
SYSCALL_HANDLER(fsetxattr);
SYSCALL_HANDLER(fstat);
SYSCALL_HANDLER(fstatfs);
SYSCALL_HANDLER(fsync);
SYSCALL_HANDLER(ftruncate);
SYSCALL_HANDLER(open);
SYSCALL_HANDLER(syncfs);

#endif /* SRC_MAIN_HOST_SYSCALL_FILE_H_ */
