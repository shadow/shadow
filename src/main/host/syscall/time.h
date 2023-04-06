/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_TIME_H_
#define SRC_MAIN_HOST_SYSCALL_TIME_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(clock_nanosleep);
SYSCALL_HANDLER(nanosleep);

#endif /* SRC_MAIN_HOST_SYSCALL_TIME_H_ */
