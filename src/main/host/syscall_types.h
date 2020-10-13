#ifndef SHD_SYSCALL_TYPES_H_
#define SHD_SYSCALL_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

// We use this to get some type safety for pointers in the plugin's address
// space. In particular we want to avoid dereferencing them directly.
struct _PluginPtr {
    uint64_t val;
};

// A virtual address in the plugin's address space
typedef struct _PluginPtr PluginVirtualPtr;
// Deprecated; use PluginVirtualPtr instead
typedef struct _PluginPtr PluginPtr;
// A phyiscal address that should be unique to the machine
typedef struct _PluginPtr PluginPhysicalPtr;

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

/* This is an opaque structure holding the state needed to resume a thread
 * previously blocked by a syscall. Any syscall that returns SYSCALL_BLOCK
 * should include a SysCallCondition by which the thread should be unblocked. */
typedef struct _SysCallCondition SysCallCondition;

typedef struct _SysCallReturn {
    SysCallReturnState state;
    // Only valid for state SYSCALL_DONE.
    SysCallReg retval;
    // Only valid for state SYSCALL_BLOCK
    SysCallCondition* cond;
} SysCallReturn;

#endif
