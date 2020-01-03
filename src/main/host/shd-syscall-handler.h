/*
 * shd-syscall-handler.h
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

typedef struct _SysCallHandler SysCallHandler;

#include "main/host/shd-thread.h"
#include "main/host/process.h"
#include "main/host/host.h"

SysCallHandler* syscallhandler_new(Host* host, Process* process);
void syscallhandler_ref(SysCallHandler* sys);
void syscallhandler_unref(SysCallHandler* sys);

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

unsigned int syscallhandler_sleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        unsigned int sec);
int syscallhandler_usleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        unsigned int usec);
int syscallhandler_nanosleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        const struct timespec *req, struct timespec *rem);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
