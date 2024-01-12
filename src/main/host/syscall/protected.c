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

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/syscall/syscall_condition.h"

int _syscallhandler_validateLegacyFile(LegacyFile* descriptor, LegacyFileType expectedType) {
    if (descriptor) {
        Status status = legacyfile_getStatus(descriptor);

        if (status & FileState_CLOSED) {
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
