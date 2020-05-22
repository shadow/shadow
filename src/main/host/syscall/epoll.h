/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_EPOLL_H_
#define SRC_MAIN_HOST_SYSCALL_EPOLL_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(epoll_create);
SYSCALL_HANDLER(epoll_create1);
SYSCALL_HANDLER(epoll_ctl);
SYSCALL_HANDLER(epoll_wait);

#endif /* SRC_MAIN_HOST_SYSCALL_EPOLL_H_ */
