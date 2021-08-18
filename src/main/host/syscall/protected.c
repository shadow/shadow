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
#include "main/host/descriptor/timer.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"

static const Timer* _syscallhandler_timeout(const SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    SysCallCondition* cond = thread_getSysCallCondition(sys->thread);
    if (!cond) {
        return NULL;
    }

    return syscallcondition_timeout(cond);
}

bool _syscallhandler_isListenTimeoutPending(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    const Timer* timeout = _syscallhandler_timeout(sys);
    if (!timeout) {
        return false;
    }

    struct itimerspec value = {0};

    gint result = timer_getTime(timeout, &value);
    utility_assert(result == 0);

    return value.it_value.tv_sec > 0 || value.it_value.tv_nsec > 0;
}

bool _syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    const Timer* timeout = _syscallhandler_timeout(sys);
    if (!timeout) {
        return false;
    }

    /* Note that the timer is "readable" if it has a positive
     * expiration count; this call does not adjust the status. */
    return timer_getExpirationCount(timeout) > 0;
}

bool _syscallhandler_wasBlocked(const SysCallHandler* sys) { return sys->blockedSyscallNR >= 0; }

int _syscallhandler_validateDescriptor(LegacyDescriptor* descriptor,
                                       LegacyDescriptorType expectedType) {
    if (descriptor) {
        Status status = descriptor_getStatus(descriptor);

        if (status & STATUS_DESCRIPTOR_CLOSED) {
            warning("descriptor handle '%i' is closed",
                    descriptor_getHandle(descriptor));
            return -EBADF;
        }

        LegacyDescriptorType type = descriptor_getType(descriptor);

        if (expectedType != DT_NONE && type != expectedType) {
            warning("descriptor handle '%i' is of type %i, expected type %i",
                    descriptor_getHandle(descriptor), type, expectedType);
            return -EINVAL;
        }

        return 0;
    } else {
        return -EBADF;
    }
}
