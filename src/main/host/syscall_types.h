#ifndef SHD_SYSCALL_TYPES_H_
#define SHD_SYSCALL_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

// A virtual address in the plugin's address space
typedef struct _PluginVirtualPtr PluginVirtualPtr;
// Deprecated; use PluginVirtualPtr instead
typedef struct _PluginVirtualPtr PluginPtr;
// A physical address that should be unique to the machine
typedef struct _PluginPhysicalPtr PluginPhysicalPtr;

struct _PluginVirtualPtr {
    uint64_t val;
};

struct _PluginPhysicalPtr {
    uint64_t val;
};

// A register used for input/output in a syscall.
typedef union _SysCallReg {
    int64_t as_i64;
    uint64_t as_u64;
    PluginPtr as_ptr;
} SysCallReg;

typedef struct _SysCallArgs {
    // SYS_* from sys/syscall.h.
    // (mostly included from
    // /usr/include/x86_64-linux-gnu/bits/syscall.h)
    long number;
    SysCallReg args[6];
} SysCallArgs;

typedef enum {
    // Done executing the syscall; ready to let the plugin thread resume.
    SYSCALL_DONE,
    // We don't have the result yet.
    SYSCALL_BLOCK,
    // Direct plugin to make the syscall natively.
    SYSCALL_NATIVE
} SysCallReturnState;

const char* syscallreturnstate_str(SysCallReturnState s);

/* This is an opaque structure holding the state needed to resume a thread
 * previously blocked by a syscall. Any syscall that returns SYSCALL_BLOCK
 * should include a SysCallCondition by which the thread should be unblocked. */
typedef struct _SysCallCondition SysCallCondition;

typedef struct {
    SysCallReg retval;
    // Only meaningful when `retval` is -EINTR.
    //
    // Whether the interrupted syscall is restartable.
    bool restartable;
} SysCallReturnDone;

typedef struct {
    SysCallCondition* cond;
    // True if the syscall is restartable in the case that it was interrupted by
    // a signal. e.g. if the syscall was a `read` operation on a socket without
    // a configured timeout. See socket(7).
    bool restartable;
} SysCallReturnBlocked;

union SysCallReturnBody {
    SysCallReturnDone done;
    SysCallReturnBlocked blocked;
};

typedef struct _SysCallReturn {
    SysCallReturnState state;
    // We need to name both the union type and the field for the Rust bindings
    // to work well.
    //
    // Avoid accessing directly; use the helper functions below instead.
    union SysCallReturnBody u;
} SysCallReturn;

SysCallReturn syscallreturn_makeDone(SysCallReg retval);
SysCallReturn syscallreturn_makeDoneI64(int64_t retval);
SysCallReturn syscallreturn_makeDoneU64(uint64_t retval);
SysCallReturn syscallreturn_makeDonePtr(PluginPtr retval);
SysCallReturn syscallreturn_makeDoneErrno(int err);
SysCallReturn syscallreturn_makeInterrupted(bool restartable);
SysCallReturn syscallreturn_makeBlocked(SysCallCondition* cond, bool restartable);
SysCallReturn syscallreturn_makeNative();

SysCallReturnBlocked* syscallreturn_blocked(SysCallReturn* ret);
SysCallReturnDone* syscallreturn_done(SysCallReturn* ret);
#endif
