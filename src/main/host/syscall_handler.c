/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/process.h"
#include "main/host/syscall/fcntl.h"
#include "main/host/syscall/fileat.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/uio.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_numbers.h"
#include "main/host/syscall_types.h"
#include "main/utility/syscall.h"

static bool _countSyscalls = false;
ADD_CONFIG_HANDLER(config_getUseSyscallCounters, _countSyscalls)

SysCallHandler* syscallhandler_new(HostId hostId, pid_t processId, pid_t threadId) {
    SysCallHandler* sys = malloc(sizeof(SysCallHandler));

    *sys = (SysCallHandler){
        .hostId = hostId,
        .processId = processId,
        .threadId = threadId,
        .syscall_handler_rs = rustsyscallhandler_new(hostId, processId, threadId, _countSyscalls),
    };

    MAGIC_INIT(sys);

    worker_count_allocation(SysCallHandler);
    return sys;
}

void syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if (sys->syscall_handler_rs) {
        rustsyscallhandler_free(sys->syscall_handler_rs);
    }

    MAGIC_CLEAR(sys);
    free(sys);
    worker_count_deallocation(SysCallHandler);
}

///////////////////////////////////////////////////////////
// Single public API function for calling Shadow syscalls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_make_syscall(SysCallHandler* sys, const SysCallArgs* args) {
    MAGIC_ASSERT(sys);

    SyscallHandler* handler = sys->syscall_handler_rs;
    sys->syscall_handler_rs = NULL;
    SyscallReturn scr = rustsyscallhandler_syscall(handler, sys, args);
    sys->syscall_handler_rs = handler;

    return scr;
}
