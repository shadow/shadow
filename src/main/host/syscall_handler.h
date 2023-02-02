/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

#include <glib.h>
#include <sys/time.h>

typedef struct _SysCallHandler SysCallHandler;

#include "main/host/process.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

SysCallHandler* syscallhandler_new(const Host* host, const ProcessRefCell* process, const ThreadRc* thread);
void syscallhandler_ref(SysCallHandler* sys);
void syscallhandler_unref(SysCallHandler* sys);
SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys,
                                          const SysCallArgs* args);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
