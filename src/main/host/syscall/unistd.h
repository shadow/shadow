/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_UNISTD_H_
#define SRC_MAIN_HOST_SYSCALL_UNISTD_H_

#include "main/host/syscall_handler.h"

SysCallReturn syscallhandler_getpid(SysCallHandler* sys,
                                          const SysCallArgs* args);

SysCallReturn syscallhandler_uname(SysCallHandler* sys,
                                          const SysCallArgs* args);

#endif /* SRC_MAIN_HOST_SYSCALL_UNISTD_H_ */
