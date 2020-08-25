
#include "clone.h"

#include <stdlib.h>

#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

SysCallReturn syscallhandler_clone(SysCallHandler* sys, const SysCallArgs* args) {
    utility_assert(sys && args);

    Thread* thr = thread_clone(sys->thread, args);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = thread_getID(thr)};
}
