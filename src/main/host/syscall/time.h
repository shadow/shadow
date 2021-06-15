/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_TIME_H_
#define SRC_MAIN_HOST_SYSCALL_TIME_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(clock_gettime);
SYSCALL_HANDLER(gettimeofday);
SYSCALL_HANDLER(nanosleep);
SYSCALL_HANDLER(time);

#endif /* SRC_MAIN_HOST_SYSCALL_TIME_H_ */
