/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "clone.h"

#include <errno.h>
#include <stdlib.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/worker.h"
#include "main/host/syscall/protected.h"
#include "main/utility/utility.h"

SyscallReturn syscallhandler_gettid(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    return syscallreturn_makeDoneI64(sys->threadId);
}
