/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/uio.h"

#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/unistd.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateVecParams(SysCallHandler* sys, int fd, PluginPtr iovPtr,
                                             unsigned long iovlen, off_t offset,
                                             LegacyFile** desc_out, struct iovec** iov_out) {
    /* Get the descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(_syscallhandler_getProcess(sys), fd);
    if (!desc) {
        return -EBADF;
    }

    /* Validate vector length param. */
    if (iovlen == 0) {
        return 0;
    } else if (iovlen > UIO_MAXIOV) {
        return -EINVAL;
    }

    /* Make sure we have a non-null vector. */
    if (!iovPtr.val) {
        return -EFAULT;
    }

    /* We can only seek on files, otherwise its a pipe error. */
    if (legacyfile_getType(desc) != DT_FILE && offset != 0) {
        return -ESPIPE;
    }

    /* Get the vector of pointers. */
    struct iovec* iov = malloc(iovlen * sizeof(*iov));
    if (process_readPtr(_syscallhandler_getProcess(sys), iov, iovPtr, iovlen * sizeof(*iov)) != 0) {
        warning("Got unreadable pointer [%p..+%zu]", (void*)iovPtr.val, iovlen * sizeof(*iov));
        free(iov);
        return -EFAULT;
    }

    /* Check that all of the buf pointers are valid. */
    for (unsigned long i = 0; i < iovlen; i++) {
        PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (!bufPtr.val && bufSize != 0) {
            debug("Invalid NULL pointer in iovec[%ld]", i);
            free(iov);
            return -EFAULT;
        }
    }

    if (desc_out) {
        *desc_out = desc;
    }
    if (iov_out) {
        *iov_out = iov;
    }
    return 0;
}

static SysCallReturn
_syscallhandler_readvHelper(SysCallHandler* sys, int fd, PluginPtr iovPtr,
                            unsigned long iovlen, unsigned long pos_l,
                            unsigned long pos_h, int flags, bool doPreadv) {
    /* Reconstruct the offset from the high and low bits */
    pos_h = pos_h & UINT32_MAX;
    pos_l = pos_l & UINT32_MAX;
    off_t offset = (off_t)((pos_h << 32) | pos_l);

    trace("Trying to readv from fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    LegacyFile* desc = NULL;
    struct iovec* iov = NULL;
    int errcode = _syscallhandler_validateVecParams(
        sys, fd, iovPtr, iovlen, offset, &desc, &iov);
    if (errcode < 0 || iovlen == 0) {
        if (iov != NULL) {
            free(iov);
        }
        return syscallreturn_makeDoneI64(errcode);
    }

    /* Some logic depends on the descriptor type. */
    LegacyFileType dType = legacyfile_getType(desc);

    ssize_t result = 0;

    /* Now we can perform the read operations. */

    /* For non-files, we only read one buffer at a time to avoid
     * unnecessary data transfer between the plugin and Shadow. */
    size_t totalBytesWritten = 0;

    for (unsigned long i = 0; i < iovlen; i++) {
        PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (bufSize == 0) {
            /* Nothing to do if the buffer is empty. */
            continue;
        }

        switch (dType) {
            case DT_FILE:
            case DT_TCPSOCKET:
            case DT_UDPSOCKET: {
                off_t thisOffset = offset;

                if (doPreadv) {
                    thisOffset += totalBytesWritten;
                }

                SysCallReturn scr =
                    _syscallhandler_readHelper(sys, fd, bufPtr, bufSize, thisOffset, doPreadv);

                // if the above syscall handler created any pointers, we may
                // need to flush them before calling the syscall handler again
                result = process_flushPtrs(_syscallhandler_getProcess(sys));
                if (result != 0) {
                    break;
                }

                switch (scr.state) {
                    case SYSCALL_DONE: {
                        result = syscallreturn_done(&scr)->retval.as_i64;
                        break;
                    }
                    case SYSCALL_BLOCK: {
                        // assume that there was no timer, and that we're blocked on this socket
                        SysCallReturnBlocked* blocked = syscallreturn_blocked(&scr);
                        syscallcondition_unref(blocked->cond);
                        result = -EWOULDBLOCK;
                        break;
                    }
                    case SYSCALL_NATIVE: {
                        panic("recv() returned SYSCALL_NATIVE");
                    }
                }

                break;
            }
            case DT_TIMER:
            case DT_EPOLL:
            default: {
                warning("readv() not yet implemented for descriptor type %i", (int)dType);
                result = -ENOTSUP;
                break;
            }
        }

        if (result > 0) {
            totalBytesWritten += (size_t)result;
        } else {
            break;
        }
    }
    if (result >= 0 || (result == -EWOULDBLOCK && totalBytesWritten > 0)) {
        result = totalBytesWritten;
    }

    free(iov);

    if (result == -EWOULDBLOCK && !(legacyfile_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            error("Indefinitely blocking a readv of vector length %lu on "
                  "file %i at offset %li",
                  iovlen, fd, offset);
        }

        /* We need to block until the descriptor is ready to write. */
        Trigger trigger =
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_FILE_READABLE};

        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

static SysCallReturn
_syscallhandler_writevHelper(SysCallHandler* sys, int fd, PluginPtr iovPtr,
                             unsigned long iovlen, unsigned long pos_l,
                             unsigned long pos_h, int flags, bool doPwritev) {
    /* Reconstruct the offset from the high and low bits */
    pos_h = pos_h & UINT32_MAX;
    pos_l = pos_l & UINT32_MAX;
    off_t offset = (off_t)((pos_h << 32) | pos_l);

    trace("Trying to writev to fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    LegacyFile* desc = NULL;
    struct iovec* iov = NULL;
    int errcode = _syscallhandler_validateVecParams(
        sys, fd, iovPtr, iovlen, offset, &desc, &iov);
    if (errcode < 0 || iovlen == 0) {
        if (iov != NULL) {
            free(iov);
        }
        return syscallreturn_makeDoneI64(errcode);
    }

    /* Some logic depends on the descriptor type. */
    LegacyFileType dType = legacyfile_getType(desc);

    ssize_t result = 0;

    /* Now we can perform the write operations. */

    /* For non-files, we only read one buffer at a time to avoid
     * unnecessary data transfer between the plugin and Shadow. */
    size_t totalBytesWritten = 0;

    for (unsigned long i = 0; i < iovlen; i++) {
        PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (bufSize == 0) {
            /* Nothing to do if the buffer is empty. */
            continue;
        }

        switch (dType) {
            case DT_FILE:
            case DT_TCPSOCKET:
            case DT_UDPSOCKET: {
                off_t thisOffset = offset;

                if (doPwritev) {
                    thisOffset += totalBytesWritten;
                }

                SysCallReturn scr =
                    _syscallhandler_writeHelper(sys, fd, bufPtr, bufSize, thisOffset, doPwritev);

                // if the above syscall handler created any pointers, we may
                // need to flush them before calling the syscall handler again
                result = process_flushPtrs(_syscallhandler_getProcess(sys));
                if (result != 0) {
                    break;
                }

                switch (scr.state) {
                    case SYSCALL_DONE: {
                        result = syscallreturn_done(&scr)->retval.as_i64;
                        break;
                    }
                    case SYSCALL_BLOCK: {
                        // assume that there was no timer, and that we're blocked on this socket
                        SysCallReturnBlocked* blocked = syscallreturn_blocked(&scr);
                        syscallcondition_unref(blocked->cond);
                        result = -EWOULDBLOCK;
                        break;
                    }
                    case SYSCALL_NATIVE: {
                        panic("send() returned SYSCALL_NATIVE");
                    }
                }

                break;
            }
            case DT_TIMER:
            case DT_EPOLL:
            default: {
                warning("writev() not yet implemented for descriptor type %i", (int)dType);
                result = -ENOTSUP;
                break;
            }
        }

        if (result > 0) {
            totalBytesWritten += (size_t)result;
        } else {
            break;
        }
    }
    if (result >= 0 || (result == -EWOULDBLOCK && totalBytesWritten > 0)) {
        result = totalBytesWritten;
    }

    free(iov);

    if (result == -EWOULDBLOCK && !(legacyfile_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            error("Indefinitely blocking a writev of vector length %lu on "
                  "file %i at offset %li",
                  iovlen, fd, offset);
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

SysCallReturn syscallhandler_readv(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    return _syscallhandler_readvHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, 0, 0, false);
}

SysCallReturn syscallhandler_preadv(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_u64,
                                       args->args[4].as_u64, 0, true);
}

SysCallReturn syscallhandler_preadv2(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_u64,
                                       args->args[4].as_u64, args->args[5].as_i64, true);
}

SysCallReturn syscallhandler_writev(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_writevHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64, 0, 0, 0, false);
}

SysCallReturn syscallhandler_pwritev(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                        args->args[2].as_u64, args->args[3].as_u64,
                                        args->args[4].as_u64, 0, true);
}

SysCallReturn syscallhandler_pwritev2(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                        args->args[2].as_u64, args->args[3].as_u64,
                                        args->args[4].as_u64, args->args[5].as_i64, true);
}
