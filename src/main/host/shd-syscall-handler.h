/*
 * shd-syscall-handler.h
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

#include <glib.h>
#include <sys/time.h>

typedef struct _SysCallHandler SysCallHandler;

#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/shd-thread.h"
#include "main/host/shd-syscall-types.h"

SysCallHandler* syscallhandler_new(Host* host, Process* process);
void syscallhandler_ref(SysCallHandler* sys);
void syscallhandler_unref(SysCallHandler* sys);

SysCallReturn syscallhandler_make_syscall(SysCallHandler* sys, Thread* thread,
                                          const SysCallArgs* args);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
