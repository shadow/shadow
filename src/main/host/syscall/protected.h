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

#include <stdbool.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"

typedef enum {
    TIMEOUT_ABSOLUTE,
    TIMEOUT_RELATIVE,
} TimeoutType;

struct _SysCallHandler {
    /* We store pointers to the host, process, and thread that the syscall
     * handler is associated with. We typically need to makes calls into
     * these modules in order to handle syscalls. */
    Host* host;
    Process* process;
    Thread* thread;

    // For syscalls implemented in rust. Will eventually replace the C handler.
    SyscallHandler* syscall_handler_rs;

    /* We use this epoll to service syscalls that need to block on the status
     * of multiple descriptors, like poll. */
    Epoll* epoll;

    /* If we are currently blocking a specific syscall, i.e., waiting for
     * a socket to be readable/writable or waiting for a timeout, the
     * syscall number of that function is stored here. The value is set
     * to negative to indicate that no syscalls are currently blocked. */
    long blockedSyscallNR;

    // TODO: if we build bindings on the fly, uncomment the ifdef and endif so that this timer
    // object is not included in the struct unless necessary.
    // https://github.com/shadow/shadow/issues/1158
    //#ifdef USE_PERF_TIMERS
    /* Used to track the time elapsed while handling a syscall. */
    GTimer* perfTimer;
    /* The cumulative time consumed while handling the current syscall.
     * This includes the time from previous calls that ended up blocking. */
    gdouble perfSecondsCurrent;
    /* The total time elapsed while handling all syscalls. */
    gdouble perfSecondsTotal;
    //#endif
    /* The total number of syscalls that we have handled. */
    long numSyscalls;
    // A counter for individual syscalls
    Counter* syscall_counter;

    // In some cases the syscallhandler comples, but we block the caller anyway
    // to move time forward. This stores the result of the completed syscall, to
    // be returned when the caller resumes.
    bool havePendingResult;
    SysCallReturn pendingResult;

    int referenceCount;

    // Since this structure is shared with Rust, we should always include the magic struct
    // member so that the struct is always the same size regardless of compile-time options.
    MAGIC_DECLARE_ALWAYS;
};

/* Amount of data to transfer between Shadow and the plugin for each
 * send/recv or read/write syscall. It would be more efficient to dynamically
 * compute how much we can read/write rather than using this static size.
 * TODO: remove this when we switch to dynamic size calculations. */
#define SYSCALL_IO_BUFSIZE (1024 * 1024 * 10) // 10 MiB

/* Use this to define the syscalls that a particular handler implements.
 * The functions defined with this macro should never be called outside
 * of syscall_handler.c. */
#define SYSCALL_HANDLER(s)                                                     \
    SysCallReturn syscallhandler_##s(                                          \
        SysCallHandler* sys, const SysCallArgs* args);

CEmulatedTime _syscallhandler_getTimeout(const SysCallHandler* sys);
bool _syscallhandler_isListenTimeoutPending(SysCallHandler* sys);
bool _syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys);
bool _syscallhandler_wasBlocked(const SysCallHandler* sys);
int _syscallhandler_validateLegacyFile(LegacyFile* descriptor, LegacyFileType expectedType);

#endif /* SRC_MAIN_HOST_SYSCALL_PROTECTED_H_ */
