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
SYSCALL_HANDLER(getsockopt);
SYSCALL_HANDLER(listen);
SYSCALL_HANDLER(recvfrom);
SYSCALL_HANDLER(sendto);
SYSCALL_HANDLER(setsockopt);
SYSCALL_HANDLER(shutdown);
SYSCALL_HANDLER(socket);
SYSCALL_HANDLER(socketpair);

/* Protected helper to allow read(sockfd) to redirect here. */
SysCallReturn _syscallhandler_recvfromHelper(SysCallHandler* sys, int sockfd,
                                             PluginPtr bufPtr, size_t bufSize,
                                             int flags, PluginPtr srcAddrPtr,
                                             PluginPtr addrlenPtr);

/* Protected helper to allow write(sockfd) to redirect here. */
SysCallReturn _syscallhandler_sendtoHelper(SysCallHandler* sys, int sockfd,
                                           PluginPtr bufPtr, size_t bufSize,
                                           int flags, PluginPtr destAddrPtr,
                                           socklen_t addrlen);

#endif /* SRC_MAIN_HOST_SYSCALL_SOCKET_H_ */
