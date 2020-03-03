#ifndef SHD_SYSCALL_TYPES_H_
#define SHD_SYSCALL_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

// A register used for input/output in a syscall.
typedef union _SysCallReg {
    int64_t as_i64;
    uint64_t as_u64;
    void* as_ptr;
    const void* as_cptr;
} SysCallReg;

typedef struct _SysCallArgs {
    // SYS_* from sys/syscall.h.
    // (mostly included from
    // /usr/include/x86_64-linux-gnu/bits/syscall.h)
    long number;
    SysCallReg args[5];
} SysCallArgs;

typedef struct _SysCallReturn {
    bool have_retval;
    // Only valid if have_retval is true.
    // Otherwise, syscall hasn't completed yet and should block.
    SysCallReg retval;
} SysCallReturn;

#endif
