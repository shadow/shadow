/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/unistd.h"

#include <errno.h>
#include <stdio.h>
#include <sys/utsname.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

SyscallReturn _syscallhandler_readHelper(SyscallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                         size_t bufSize, off_t offset, bool doPread) {
    trace(
        "trying to read %zu bytes on fd %i at offset %li", bufSize, fd, offset);

    /* Get the descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), fd);
    if (!desc) {
        return syscallreturn_makeDoneErrno(EBADF);
    }

    /* Some logic depends on the descriptor type. */
    LegacyFileType dType = legacyfile_getType(desc);

    /* We can only seek on files, otherwise its a pipe error. */
    if (dType != DT_FILE && offset != 0) {
        return syscallreturn_makeDoneErrno(ESPIPE);
    }

    /* Divert io on sockets to socket handler to pick up special checks. */
    if (dType == DT_TCPSOCKET) {
        panic("Should have handled this in the rust syscall handler");
    }

    /* Now it's an error if the descriptor is closed. */
    int errorCode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
    if (errorCode != 0) {
        return syscallreturn_makeDoneErrno(-errorCode);
    }
    utility_debugAssert(desc);

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);

    ssize_t result = 0;
    switch (dType) {
        case DT_FILE:
            if (!doPread) {
                utility_debugAssert(offset == 0);
                result = regularfile_read(
                    (RegularFile*)desc, rustsyscallhandler_getHost(sys),
                    process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded);
            } else {
                result = regularfile_pread(
                    (RegularFile*)desc, rustsyscallhandler_getHost(sys),
                    process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded, offset);
            }
            break;
        case DT_TCPSOCKET:
            // We already diverted these to the socket handler above.
            utility_debugAssert(0);
            break;
        case DT_EPOLL:
        default:
            warning("read(%d) not yet implemented for descriptor type %i", fd, (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(legacyfile_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            error("Indefinitely blocking a read of %zu bytes on file %i at "
                  "offset %li",
                  bufSize, fd, offset);
        }

        /* We need to block until the descriptor is ready to read. */
        Trigger trigger =
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .state = FileState_READABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

SyscallReturn _syscallhandler_writeHelper(SyscallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                          size_t bufSize, off_t offset, bool doPwrite) {
    trace("trying to write %zu bytes on fd %i at offset %li", bufSize, fd,
          offset);

    /* Get the descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), fd);
    if (!desc) {
        return syscallreturn_makeDoneErrno(EBADF);
    }

    /* Some logic depends on the descriptor type. */
    LegacyFileType dType = legacyfile_getType(desc);

    /* We can only seek on files, otherwise its a pipe error. */
    if (dType != DT_FILE && offset != 0) {
        return syscallreturn_makeDoneErrno(ESPIPE);
    }

    /* Divert io on sockets to socket handler to pick up special checks. */
    if (dType == DT_TCPSOCKET) {
        panic("Should have handled this in the rust syscall handler");
    }

    /* Now it's an error if the descriptor is closed. */
    gint errorCode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
    if (errorCode != 0) {
        return syscallreturn_makeDoneErrno(-errorCode);
    }
    utility_debugAssert(desc);

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);

    ssize_t result = 0;
    switch (dType) {
        case DT_FILE:
            if (!doPwrite) {
                utility_debugAssert(offset == 0);
                result = regularfile_write(
                    (RegularFile*)desc,
                    process_getReadablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded);
            } else {
                result = regularfile_pwrite(
                    (RegularFile*)desc,
                    process_getReadablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded, offset);
            }
            break;
        case DT_TCPSOCKET:
            // We already diverted these to the socket handler above.
            utility_debugAssert(0);
            break;
        case DT_EPOLL:
        default:
            warning("write(%d) not yet implemented for descriptor type %i", fd, (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(legacyfile_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            error("Indefinitely blocking a write of %zu bytes on file %i at "
                  "offset %li",
                  bufSize, fd, offset);
        }

        /* We need to block until the descriptor is ready to write. */
        Trigger trigger =
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .state = FileState_WRITABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_read(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_readHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, false);
}

SyscallReturn syscallhandler_pread64(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_readHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                      args->args[2].as_u64, args->args[3].as_i64, true);
}

SyscallReturn syscallhandler_write(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_writeHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, false);
}

SyscallReturn syscallhandler_pwrite64(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_writeHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_i64, true);
}