/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/unistd.h"

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
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

SyscallReturn _syscallhandler_readHelper(SysCallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                         size_t bufSize, off_t offset, bool doPread) {
    trace(
        "trying to read %zu bytes on fd %i at offset %li", bufSize, fd, offset);

    /* Get the descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(_syscallhandler_getThread(sys), fd);
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
    if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
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
                    (RegularFile*)desc, _syscallhandler_getHost(sys),
                    process_getWriteablePtr(_syscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded);
            } else {
                result = regularfile_pread(
                    (RegularFile*)desc, _syscallhandler_getHost(sys),
                    process_getWriteablePtr(_syscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded, offset);
            }
            break;
        case DT_TIMER:
            if (doPread) {
                result = -ESPIPE;
            } else {
                panic("Should have handled this in the rust syscall handler");
            }
            break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
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
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_FILE_READABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

SyscallReturn _syscallhandler_writeHelper(SysCallHandler* sys, int fd, UntypedForeignPtr bufPtr,
                                          size_t bufSize, off_t offset, bool doPwrite) {
    trace("trying to write %zu bytes on fd %i at offset %li", bufSize, fd,
          offset);

    /* Get the descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(_syscallhandler_getThread(sys), fd);
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
    if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
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
                    process_getReadablePtr(_syscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded);
            } else {
                result = regularfile_pwrite(
                    (RegularFile*)desc,
                    process_getReadablePtr(_syscallhandler_getProcess(sys), bufPtr, sizeNeeded),
                    sizeNeeded, offset);
            }
            break;
        case DT_TIMER: result = -EINVAL; break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
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
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_FILE_WRITABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_read(SysCallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_readHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, false);
}

SyscallReturn syscallhandler_pread64(SysCallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_readHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                      args->args[2].as_u64, args->args[3].as_i64, true);
}

SyscallReturn syscallhandler_write(SysCallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_writeHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, false);
}

SyscallReturn syscallhandler_pwrite64(SysCallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_writeHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_i64, true);
}

SyscallReturn syscallhandler_exit_group(SysCallHandler* sys, const SysCallArgs* args) {
    trace("Exit group with exit code %ld", args->args[0].as_i64);
    return syscallreturn_makeNative();
}

SyscallReturn syscallhandler_getpid(SysCallHandler* sys, const SysCallArgs* args) {
    // We can't handle this natively in the plugin if we want determinism
    pid_t pid = sys->processId;
    return syscallreturn_makeDoneI64(pid);
}

SyscallReturn syscallhandler_set_tid_address(SysCallHandler* sys, const SysCallArgs* args) {
    UntypedForeignPtr tidptr = args->args[0].as_ptr; // int*
    thread_setTidAddress(_syscallhandler_getThread(sys), tidptr);
    return syscallreturn_makeDoneI64(sys->threadId);
}

SyscallReturn syscallhandler_uname(SysCallHandler* sys, const SysCallArgs* args) {
    struct utsname* buf = NULL;

    buf = process_getWriteablePtr(
        _syscallhandler_getProcess(sys), args->args[0].as_ptr, sizeof(*buf));
    if (!buf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const gchar* hostname = host_getName(_syscallhandler_getHost(sys));

    snprintf(buf->sysname, _UTSNAME_SYSNAME_LENGTH, "shadowsys");
    snprintf(buf->nodename, _UTSNAME_NODENAME_LENGTH, "%s", hostname);
    snprintf(buf->release, _UTSNAME_RELEASE_LENGTH, "shadowrelease");
    snprintf(buf->version, _UTSNAME_VERSION_LENGTH, "shadowversion");
    snprintf(buf->machine, _UTSNAME_MACHINE_LENGTH, "shadowmachine");

    return syscallreturn_makeDoneI64(0);
}
