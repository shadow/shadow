/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

#include <glib.h>
#include <sys/time.h>

typedef struct _SysCallHandler SysCallHandler;

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings-opaque.h"
#include "main/host/process.h"

SysCallHandler* syscallhandler_new(HostId hostId, pid_t processId, pid_t threadId);
void syscallhandler_free(SysCallHandler* sys);
SyscallReturn syscallhandler_make_syscall(SysCallHandler* sys, const SysCallArgs* args);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
