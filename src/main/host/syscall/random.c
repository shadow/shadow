/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/random.h"

#include <errno.h>

#include "main/host/host.h"
#include "main/host/thread.h"
#include "main/host/syscall/protected.h"
#include "main/utility/random.h"
#include "support/logger/logger.h"


///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_getrandom(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    PluginPtr bufPtr = args->args[0].as_ptr; // char*
    size_t count = args->args[1].as_u64;
    // We ignore the flags arg, because we use the same random source for both
    // random and urandom, and it never blocks anyway.

    debug("Trying to read %zu random bytes.", count);

    if(!bufPtr.val) {
        info("Invalid buffer.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    // Get the buffer where we can copy the random bytes
    char* buf = memorymanager_getWriteablePtr(sys->memoryManager, sys->thread, bufPtr, count);

    // Get the source from the host to maintain determinism.
    Random* rng = host_getRandom(sys->host);
    utility_assert(rng != NULL);
    
    // We always get the number of bytes we requested.
    random_nextNBytes(rng, buf, count);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = count};
}

