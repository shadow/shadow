/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/protected.h"

#include <errno.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"

EmulatedTime _syscallhandler_getTimeout(const SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    SysCallCondition* cond = thread_getSysCallCondition(sys->thread);
    if (!cond) {
        return EMUTIME_INVALID;
    }

    return syscallcondition_getTimeout(cond);
}

bool _syscallhandler_isListenTimeoutPending(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    return _syscallhandler_getTimeout(sys) != EMUTIME_INVALID;
}

bool _syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    EmulatedTime timeout = _syscallhandler_getTimeout(sys);
    return timeout != EMUTIME_INVALID && worker_getCurrentEmulatedTime() >= timeout;
}

bool _syscallhandler_wasBlocked(const SysCallHandler* sys) { return sys->blockedSyscallNR >= 0; }

int _syscallhandler_validateDescriptor(LegacyDescriptor* descriptor,
                                       LegacyDescriptorType expectedType) {
    if (descriptor) {
        Status status = descriptor_getStatus(descriptor);

        if (status & STATUS_DESCRIPTOR_CLOSED) {
            warning("descriptor %p is closed", descriptor);
            return -EBADF;
        }

        LegacyDescriptorType type = descriptor_getType(descriptor);

        if (expectedType != DT_NONE && type != expectedType) {
            warning(
                "descriptor %p is of type %i, expected type %i", descriptor, type, expectedType);
            return -EINVAL;
        }

        return 0;
    } else {
        return -EBADF;
    }
}
