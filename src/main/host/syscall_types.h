#ifndef SHD_SYSCALL_TYPES_H_
#define SHD_SYSCALL_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings-opaque.h"

// A virtual address in the plugin's address space
typedef PluginPtr PluginVirtualPtr;

typedef enum {
    // Done executing the syscall; ready to let the plugin thread resume.
    SYSCALL_DONE,
    // We don't have the result yet.
    SYSCALL_BLOCK,
    // Direct plugin to make the syscall natively.
    SYSCALL_NATIVE
} SyscallReturnState;

const char* syscallreturnstate_str(SyscallReturnState s);

/* This is an opaque structure holding the state needed to resume a thread
 * previously blocked by a syscall. Any syscall that returns SYSCALL_BLOCK
 * should include a SysCallCondition by which the thread should be unblocked. */
typedef struct _SysCallCondition SysCallCondition;

typedef struct _SyscallReturn {
    SyscallReturnState state;
    // We need to name both the union type and the field for the Rust bindings
    // to work well.
    //
    // Avoid accessing directly; use the helper functions below instead.
    SyscallReturnBody u;
} SyscallReturn;

SyscallReturn syscallreturn_makeDone(SysCallReg retval);
SyscallReturn syscallreturn_makeDoneI64(int64_t retval);
SyscallReturn syscallreturn_makeDoneU64(uint64_t retval);
SyscallReturn syscallreturn_makeDonePtr(PluginPtr retval);
SyscallReturn syscallreturn_makeDoneErrno(int err);
SyscallReturn syscallreturn_makeInterrupted(bool restartable);
SyscallReturn syscallreturn_makeBlocked(SysCallCondition* cond, bool restartable);
SyscallReturn syscallreturn_makeNative();

SyscallReturnBlocked* syscallreturn_blocked(SyscallReturn* ret);
SyscallReturnDone* syscallreturn_done(SyscallReturn* ret);
#endif
