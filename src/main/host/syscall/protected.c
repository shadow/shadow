/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/protected.h"

#include <errno.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/time.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/syscall_condition.h"

CEmulatedTime _syscallhandler_getTimeout(const SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    SysCallCondition* cond = thread_getSysCallCondition(_syscallhandler_getThread(sys));
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

    CEmulatedTime timeout = _syscallhandler_getTimeout(sys);
    return timeout != EMUTIME_INVALID && worker_getCurrentEmulatedTime() >= timeout;
}

bool _syscallhandler_wasBlocked(const SysCallHandler* sys) { return sys->blockedSyscallNR >= 0; }

int _syscallhandler_validateLegacyFile(LegacyFile* descriptor, LegacyFileType expectedType) {
    if (descriptor) {
        Status status = legacyfile_getStatus(descriptor);

        if (status & STATUS_FILE_CLOSED) {
            // A file that is referenced in the descriptor table should never
            // be a closed file. File handles (fds) are handles to open files,
            // so if we have a file handle to a closed file, then there's an
            // error somewhere in Shadow. Shadow's TCP sockets do close
            // themselves even if there are still file handles (see
            // `_tcp_endOfFileSignalled`), so we can't make this a panic.
            warning("descriptor %p is closed", descriptor);
            return -EBADF;
        }

        LegacyFileType type = legacyfile_getType(descriptor);

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
