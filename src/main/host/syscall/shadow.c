/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/shadow.h"

#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strings.h>

#include "lib/logger/logger.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/core/support/config_handlers.h"
#include "main/core/worker.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_types.h"
#include "main/routing/address.h"

static bool _useMM = true;
ADD_CONFIG_HANDLER(config_getUseMemoryManager, _useMM)

SysCallReturn syscallhandler_shadow_hostname_to_addr_ipv4(SysCallHandler* sys,
                                                          const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    PluginPtr name_ptr = args->args[0].as_ptr;
    size_t name_len = args->args[1].as_u64;
    PluginPtr addr_ptr = args->args[2].as_ptr;
    size_t addr_len = args->args[3].as_u64;

    trace("Handling custom syscall shadow_hostname_to_addr_ipv4");

    if (!name_ptr.val || !addr_ptr.val || addr_len < sizeof(uint32_t)) {
        trace("Invalid argument detected, returning EINVAL");
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    const char* name;
    int rv = process_getReadableString(
        sys->process, name_ptr, name_len + 1 /* NULL byte */, &name, &name_len);
    if (rv != 0) {
        return syscallreturn_makeDoneErrno(-rv);
    }

    if (strcasecmp(name, "localhost") == 0) {
        // Loopback address in network order.
        uint32_t* addr = process_getWriteablePtr(sys->process, addr_ptr, addr_len);
        *addr = htonl(INADDR_LOOPBACK);
        trace("Returning loopback address for localhost");
        return syscallreturn_makeDoneI64(0);
    }

    const Address* address;

    if (strncasecmp(name, host_getName(_syscallhandler_getHost(sys)), MIN(name_len, NI_MAXHOST)) ==
        0) {
        trace("Using default address for my own hostname %s", name);
        address = host_getDefaultAddress(_syscallhandler_getHost(sys));
    } else {
        trace("Looking up name %s", name);
        address = worker_resolveNameToAddress(name);
    }

    if (address) {
        trace("Found address %s for name %s", address_toString(address), name);

        uint32_t ip = address_toNetworkIP(address);

        // Release the readable pointer so that we can get a writable pointer.
        process_flushPtrs(sys->process);

        uint32_t* addr = process_getWriteablePtr(sys->process, addr_ptr, addr_len);
        *addr = ip;

        return syscallreturn_makeDoneI64(0);
    } else {
        trace("Unable to find address for name %s", name);
        // return EFAULT like gethostname
        return syscallreturn_makeDoneErrno(EFAULT);
    }
}

static SysCallReturn _syscallhandler_get_shmem_block(SysCallHandler* sys, const SysCallArgs* args,
                                                     ShMemBlock* block) {
    if (!block) {
        return syscallreturn_makeDoneErrno(ENODEV);
    }

    PluginPtr shm_blk_pptr = args->args[0].as_ptr;

    ShMemBlockSerialized* shm_blk_ptr =
        process_getWriteablePtr(sys->process, shm_blk_pptr, sizeof(*shm_blk_ptr));
    *shm_blk_ptr = shmemallocator_globalBlockSerialize(block);

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_shadow_get_ipc_blk(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    trace("handling shadow_get_ipc_blk syscall");
    return _syscallhandler_get_shmem_block(sys, args, thread_getIPCBlock(sys->thread));
}

SysCallReturn syscallhandler_shadow_get_shm_blk(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    trace("handling shadow_get_shm_blk syscall");
    return _syscallhandler_get_shmem_block(sys, args, thread_getShMBlock(sys->thread));
}

SysCallReturn syscallhandler_shadow_init_memory_manager(SysCallHandler* sys, const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    if (_useMM) {
        trace("Initializing memory manager");
        memorymanager_initMapperIfNeeded(process_getMemoryManager(sys->process), sys->thread);
    } else {
        trace("Not initializing memory manager");
    }
    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_shadow_yield(SysCallHandler* sys, const SysCallArgs* args) {
    return syscallreturn_makeDoneI64(0);
}
