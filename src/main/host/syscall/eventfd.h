/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_EVENTFD_H_
#define SRC_MAIN_HOST_SYSCALL_EVENTFD_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(eventfd);
SYSCALL_HANDLER(eventfd2);

#endif /* SRC_MAIN_HOST_SYSCALL_EVENTFD_H_ */
