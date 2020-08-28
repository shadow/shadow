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
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
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

    /* Used to track the time elapsed while handling a syscall. */
    GTimer* perfTimer;
    /* The cumulative time consumed while handling the current syscall.
     * This includes the time from previous calls that ended up blocking. */
    gdouble perfSecondsCurrent;
    /* The total time elapsed while handling all syscalls. */
    gdouble perfSecondsTotal;
    /* The total number of syscalls that we have handled. */
    long numSyscalls;

    int referenceCount;

    MAGIC_DECLARE;
};

/* Amount of data to transfer between Shadow and the plugin for each
 * send/recv or read/write syscall. It would be more efficient to dynamically
 * compute how much we can read/write rather than using this static size.
 * TODO: remove this when we switch to dynamic size calculations. */
#define SYSCALL_IO_BUFSIZE (1024 * 16) // 16 KiB

/* Use this to define the syscalls that a particular handler implements.
 * The functions defined with this macro should never be called outside
 * of syscall_handler.c. */
#define SYSCALL_HANDLER(s)                                                     \
    SysCallReturn syscallhandler_##s(                                          \
        SysCallHandler* sys, const SysCallArgs* args);

void _syscallhandler_setListenTimeout(SysCallHandler* sys,
                                      const struct timespec* timeout);
void _syscallhandler_setListenTimeoutMillis(SysCallHandler* sys,
                                            gint timeout_ms);
int _syscallhandler_isListenTimeoutPending(SysCallHandler* sys);
int _syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys);
int _syscallhandler_wasBlocked(const SysCallHandler* sys);
int _syscallhandler_validateDescriptor(Descriptor* descriptor,
                                       DescriptorType expectedType);

#endif /* SRC_MAIN_HOST_SYSCALL_PROTECTED_H_ */
