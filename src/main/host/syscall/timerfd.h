/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_TIMERFD_H_
#define SRC_MAIN_HOST_SYSCALL_TIMERFD_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(timerfd_create);
SYSCALL_HANDLER(timerfd_gettime);
SYSCALL_HANDLER(timerfd_settime);

#endif /* SRC_MAIN_HOST_SYSCALL_TIMERFD_H_ */
