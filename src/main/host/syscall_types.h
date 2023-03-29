#ifndef SHD_SYSCALL_TYPES_H_
#define SHD_SYSCALL_TYPES_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings-opaque.h"

const char* syscallreturnstate_str(SyscallReturn_Tag s);

/* This is an opaque structure holding the state needed to resume a thread
 * previously blocked by a syscall. Any syscall that returns SYSCALL_BLOCK
 * should include a SysCallCondition by which the thread should be unblocked. */
typedef struct _SysCallCondition SysCallCondition;

#endif
