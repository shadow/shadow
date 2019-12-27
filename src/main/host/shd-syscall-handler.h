/*
 * shd-syscall-handler.h
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

#include "main/host/host.h"

typedef struct _SystemCallHandler SystemCallHandler;

SystemCallHandler* syscallhandler_new(Host* host);
void syscallhandler_ref(SystemCallHandler* sys);
void syscallhandler_unref(SystemCallHandler* sys);

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

unsigned int syscallhandler_sleep(SystemCallHandler* sys, int threadKey,
        unsigned int sec);
int syscallhandler_usleep(SystemCallHandler* sys, int threadKey,
        unsigned int usec);
int syscallhandler_nanosleep(SystemCallHandler* sys, int threadKey,
        const struct timespec *req, struct timespec *rem);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
