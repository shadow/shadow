/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_SOCKET_H_
#define SRC_MAIN_HOST_SYSCALL_SOCKET_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(accept);
SYSCALL_HANDLER(accept4);
SYSCALL_HANDLER(bind);
SYSCALL_HANDLER(connect);
SYSCALL_HANDLER(getpeername);
SYSCALL_HANDLER(getsockname);
SYSCALL_HANDLER(listen);
SYSCALL_HANDLER(recvfrom);
SYSCALL_HANDLER(sendto);
SYSCALL_HANDLER(shutdown);
SYSCALL_HANDLER(socket);

#endif /* SRC_MAIN_HOST_SYSCALL_SOCKET_H_ */
