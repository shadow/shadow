/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_PROTECTED_H_
#define SRC_MAIN_HOST_SYSCALL_PROTECTED_H_

/*
 * Implementation details for syscall handling.
 *
 * This file should only be included by C files *implementing* syscall
 * handlers.
 */

#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"

struct _SysCallHandler {
    /* We store pointers to the host, process, and thread that the syscall
     * handler is associated with. We typically need to makes calls into
     * these modules in order to handle syscalls. */
    Host* host;
    Process* process;
    Thread* thread;

    /* Timers are used to support the timerfd syscalls (man timerfd_create);
     * they are types of descriptors on which we can listen for events.
     * Here we use it to help us handling blocking syscalls that include a
     * timeout after which we should stop blocking. */
    Timer* timer;

    /* If we are currently blocking a specific syscall, i.e., waiting for
     * a socket to be readable/writable or waiting for a timeout, the
     * syscall number of that function is stored here. The value is set
     * to negative to indicate that no syscalls are currently blocked. */
    long blockedSyscallNR;

    int referenceCount;

    MAGIC_DECLARE;
};

#endif /* SRC_MAIN_HOST_SYSCALL_PROTECTED_H_ */
