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
#include "main/bindings/c/bindings-opaque.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/process.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/utility/utility.h"

typedef enum {
    TIMEOUT_ABSOLUTE,
    TIMEOUT_RELATIVE,
} TimeoutType;

struct _SysCallHandler {
    HostId hostId;
    pid_t processId;
    pid_t threadId;

    // For syscalls implemented in rust. Will eventually replace the C handler.
    SyscallHandler* syscall_handler_rs;

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
#define SYSCALL_HANDLER(s)                                                                         \
    SyscallReturn syscallhandler_##s(SyscallHandler* sys, const SysCallArgs* args);

int _syscallhandler_validateLegacyFile(LegacyFile* descriptor, LegacyFileType expectedType);

#endif /* SRC_MAIN_HOST_SYSCALL_PROTECTED_H_ */
