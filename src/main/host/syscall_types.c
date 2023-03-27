#include "main/host/syscall_types.h"

#include "main/utility/utility.h"

SyscallReturn syscallreturn_makeDone(SysCallReg retval) {
    return (SyscallReturn){
        .state = SYSCALL_DONE,
        .u.done =
            {
                .retval = retval,
            },
    };
}

SyscallReturn syscallreturn_makeDoneI64(int64_t retval) {
    return syscallreturn_makeDone((SysCallReg){.as_i64 = retval});
}

SyscallReturn syscallreturn_makeDoneU64(uint64_t retval) {
    return syscallreturn_makeDone((SysCallReg){.as_u64 = retval});
}

SyscallReturn syscallreturn_makeDonePtr(PluginPtr retval) {
    return syscallreturn_makeDone((SysCallReg){.as_ptr = retval});
}

SyscallReturn syscallreturn_makeDoneErrno(int err) {
    // Should be a *positive* error value.
    utility_debugAssert(err > 0);
    // Should use `syscallreturn_makeInterrupted` for EINTR
    utility_debugAssert(err != EINTR);

    return syscallreturn_makeDoneI64(-err);
}

SyscallReturn syscallreturn_makeInterrupted(bool restartable) {
    return (SyscallReturn){
        .state = SYSCALL_DONE,
        .u.done =
            {
                .retval.as_i64 = -EINTR,
                .restartable = restartable,
            },
    };
}

SyscallReturn syscallreturn_makeBlocked(SysCallCondition* cond, bool restartable) {
    return (SyscallReturn){
        .state = SYSCALL_BLOCK,
        .u.blocked =
            {
                .cond = cond,
                .restartable = restartable,
            },
    };
}

SyscallReturn syscallreturn_makeNative() {
    return (SyscallReturn){
        .state = SYSCALL_NATIVE,
    };
}

SyscallReturnBlocked* syscallreturn_blocked(SyscallReturn* ret) {
    utility_alwaysAssert(ret->state == SYSCALL_BLOCK);
    return &ret->u.blocked;
}

SyscallReturnDone* syscallreturn_done(SyscallReturn* ret) {
    utility_alwaysAssert(ret->state == SYSCALL_DONE);
    return &ret->u.done;
}

const char* syscallreturnstate_str(SyscallReturnState s) {
    switch (s) {
        case SYSCALL_DONE: return "DONE";
        case SYSCALL_BLOCK: return "BLOCK";
        case SYSCALL_NATIVE: return "NATIVE";
    }
    return "UNKNOWN";
}