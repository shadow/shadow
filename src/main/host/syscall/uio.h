/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_UIO_H_
#define SRC_MAIN_HOST_SYSCALL_UIO_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(preadv);
SYSCALL_HANDLER(preadv2);
SYSCALL_HANDLER(pwritev);
SYSCALL_HANDLER(pwritev2);
SYSCALL_HANDLER(readv);
SYSCALL_HANDLER(writev);

#endif /* SRC_MAIN_HOST_SYSCALL_UIO_H_ */
