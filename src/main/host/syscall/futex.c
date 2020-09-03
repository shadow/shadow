/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/futex.h"

#include <errno.h>

#include "main/host/syscall/protected.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

SysCallReturn syscallhandler_futex(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    PluginPtr uaddr = args->args[0].as_ptr; // int*
    int futex_op = args->args[1].as_i64;
    int val = args->args[2].as_i64;
    PluginPtr timeout = args->args[3].as_ptr; // const struct timespec*, or uint32_t
    PluginPtr uaddr2 = args->args[4].as_ptr; // int*
    int val3 = args->args[5].as_i64;

    return (SysCallReturn){.state = SYSCALL_NATIVE};
}
