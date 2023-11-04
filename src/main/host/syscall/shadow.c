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
#include "main/core/worker.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_types.h"
#include "main/routing/address.h"

SyscallReturn syscallhandler_shadow_hostname_to_addr_ipv4(SysCallHandler* sys,
                                                          const SysCallArgs* args) {
    utility_debugAssert(sys && args);
    UntypedForeignPtr name_ptr = args->args[0].as_ptr;
    size_t name_len = args->args[1].as_u64;
    UntypedForeignPtr addr_ptr = args->args[2].as_ptr;
    size_t addr_len = args->args[3].as_u64;

    trace("Handling custom syscall shadow_hostname_to_addr_ipv4");

    if (!name_ptr.val || !addr_ptr.val || addr_len < sizeof(uint32_t)) {
        trace("Invalid argument detected, returning EINVAL");
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    const char* name;
    int rv = process_getReadableString(
        _syscallhandler_getProcess(sys), name_ptr, name_len + 1 /* NULL byte */, &name, &name_len);
    if (rv != 0) {
        return syscallreturn_makeDoneErrno(-rv);
    }

    if (strcasecmp(name, "localhost") == 0) {
        // Loopback address in network order.
        uint32_t* addr =
            process_getWriteablePtr(_syscallhandler_getProcess(sys), addr_ptr, addr_len);
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
        int res = process_flushPtrs(_syscallhandler_getProcess(sys));
        if (res != 0) {
            return syscallreturn_makeDoneErrno(res);
        }

        uint32_t* addr =
            process_getWriteablePtr(_syscallhandler_getProcess(sys), addr_ptr, addr_len);
        if (addr == NULL) {
            return syscallreturn_makeDoneErrno(EFAULT);
        }
        *addr = ip;

        return syscallreturn_makeDoneI64(0);
    } else {
        trace("Unable to find address for name %s", name);
        // return EFAULT like gethostname
        return syscallreturn_makeDoneErrno(EFAULT);
    }
}
