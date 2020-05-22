/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/protected.h"

#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "main/host/descriptor/timer.h"
#include "support/logger/logger.h"

void _syscallhandler_setListenTimeout(SysCallHandler* sys,
                                      const struct timespec* timeout) {
    MAGIC_ASSERT(sys);

    /* Set a non-repeating (one-shot) timer to the given timeout.
     * A NULL timeout indicates we should turn off the timer. */
    struct itimerspec value = {
        .it_value = timeout ? *timeout : (struct timespec){0},
    };

    /* This causes us to lose the previous state of the timer. */
    gint result = timer_setTime(sys->timer, 0, &value, NULL);

    if (result != 0) {
        error("syscallhandler failed to set timeout to %lu.%09lu seconds",
              (long unsigned int)value.it_value.tv_sec,
              (long unsigned int)value.it_value.tv_nsec);
        utility_assert(result == 0);
    }
}

void _syscallhandler_setListenTimeoutMillis(SysCallHandler* sys,
                                            gint timeout_ms) {
    struct timespec timeout = utility_timespecFromMillis((int64_t)timeout_ms);
    _syscallhandler_setListenTimeout(sys, &timeout);
}

int _syscallhandler_isListenTimeoutPending(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    struct itimerspec value = {0};

    gint result = timer_getTime(sys->timer, &value);
    utility_assert(result == 0);

    return value.it_value.tv_sec > 0 || value.it_value.tv_nsec > 0;
}

int _syscallhandler_didListenTimeoutExpire(const SysCallHandler* sys) {
    /* Note that the timer is "readable" if it has a positive
     * expiration count; this call does not adjust the status. */
    return timer_getExpirationCount(sys->timer) > 0;
}

int _syscallhandler_wasBlocked(const SysCallHandler* sys) {
    return sys->blockedSyscallNR >= 0;
}

int _syscallhandler_validateDescriptor(Descriptor* descriptor,
                                       DescriptorType expectedType) {
    if (descriptor) {
        DescriptorStatus status = descriptor_getStatus(descriptor);

        if (status & DS_CLOSED) {
            warning("descriptor handle '%i' is closed",
                    descriptor_getHandle(descriptor));
            return -EBADF;
        }

        DescriptorType type = descriptor_getType(descriptor);

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
