/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_FCNTL_H_
#define SRC_MAIN_HOST_SYSCALL_FCNTL_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(fcntl);
SYSCALL_HANDLER(fcntl64);

#endif /* SRC_MAIN_HOST_SYSCALL_FCNTL_H_ */
