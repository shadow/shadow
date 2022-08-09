#include "main/host/syscall_types.h"

#include "main/utility/utility.h"

SysCallReturn syscallreturn_makeDone(SysCallReg retval) {
    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .u.done =
            {
                .retval = retval,
            },
    };
}

SysCallReturn syscallreturn_makeDoneI64(int64_t retval) {
    return syscallreturn_makeDone((SysCallReg){.as_i64 = retval});
}

SysCallReturn syscallreturn_makeDoneU64(uint64_t retval) {
    return syscallreturn_makeDone((SysCallReg){.as_u64 = retval});
}

SysCallReturn syscallreturn_makeDonePtr(PluginPtr retval) {
    return syscallreturn_makeDone((SysCallReg){.as_ptr = retval});
}

SysCallReturn syscallreturn_makeDoneErrno(int err) {
    // Should be a *positive* error value.
    utility_debugAssert(err > 0);
    // Should use `syscallreturn_makeInterrupted` for EINTR
    utility_debugAssert(err != EINTR);

    return syscallreturn_makeDoneI64(-err);
}

SysCallReturn syscallreturn_makeInterrupted(bool restartable) {
    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .u.done =
            {
                .retval.as_i64 = -EINTR,
                .restartable = restartable,
            },
    };
}

SysCallReturn syscallreturn_makeBlocked(SysCallCondition* cond, bool restartable) {
    return (SysCallReturn){
        .state = SYSCALL_BLOCK,
        .u.blocked =
            {
                .cond = cond,
                .restartable = restartable,
            },
    };
}

SysCallReturn syscallreturn_makeNative() {
    return (SysCallReturn){
        .state = SYSCALL_NATIVE,
    };
}

SysCallReturnBlocked* syscallreturn_blocked(SysCallReturn* ret) {
    utility_alwaysAssert(ret->state == SYSCALL_BLOCK);
    return &ret->u.blocked;
}

SysCallReturnDone* syscallreturn_done(SysCallReturn* ret) {
    utility_alwaysAssert(ret->state == SYSCALL_DONE);
    return &ret->u.done;
}

const char* syscallreturnstate_str(SysCallReturnState s) {
    switch (s) {
        case SYSCALL_DONE: return "DONE";
        case SYSCALL_BLOCK: return "BLOCK";
        case SYSCALL_NATIVE: return "NATIVE";
    }
    return "UNKNOWN";
}