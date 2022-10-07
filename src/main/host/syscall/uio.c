/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/uio.h"

#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateVecParams(SysCallHandler* sys, int fd, PluginPtr iovPtr,
                                             unsigned long iovlen, off_t offset,
                                             LegacyFile** desc_out, struct iovec** iov_out) {
    /* Get the descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(sys->process, fd);
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
    if (process_readPtr(sys->process, iov, iovPtr, iovlen * sizeof(*iov)) != 0) {
        warning("Got unreadable pointer [%p..+%zu]", (void*)iovPtr.val, iovlen * sizeof(*iov));
        free(iov);
        return -EFAULT;
    }

    /* Check that all of the buf pointers are valid. */
    for (unsigned long i = 0; i < iovlen; i++) {
        PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (!bufPtr.val) {
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
                            unsigned long pos_h, int flags) {
    /* Reconstruct the offset from the high and low bits */
    off_t offset = (off_t)((pos_h << 32) & pos_l);

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

    /* Now we can perform the write operations. */
    if (dType == DT_FILE) {
        /* For files, we read all of the buffers from the plugin and then
         * let file pwritev handle it. */
        struct iovec* buffersv = malloc(iovlen * sizeof(*iov));

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            buffersv[i].iov_base = process_getWriteablePtr(sys->process, bufPtr, bufSize);
            buffersv[i].iov_len = bufSize;
        }

#ifdef SYS_preadv2
        result =
            regularfile_preadv2((RegularFile*)desc, sys->host, buffersv, iovlen, offset, flags);
#else
        if (flags) {
            warning("Ignoring flags");
        }
        result = regularfile_preadv((RegularFile*)desc, sys->host, buffersv, iovlen, offset);
#endif

        free(buffersv);
    } else {
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
                case DT_FILE: {
                    /* Handled above. */
                    utility_debugAssert(0);
                    break;
                }
                case DT_TCPSOCKET:
                case DT_UDPSOCKET: {
                    SysCallReturn scr = _syscallhandler_recvfromHelper(
                        sys, fd, bufPtr, bufSize, 0, (PluginPtr){0},
                        (PluginPtr){0});

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
                    warning(
                        "readv() not yet implemented for descriptor type %i",
                        (int)dType);
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
                             unsigned long pos_h, int flags) {
    /* Reconstruct the offset from the high and low bits */
    off_t offset = (off_t)((pos_h << 32) & pos_l);

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
    if (dType == DT_FILE) {
        /* For files, we read all of the buffers from the plugin and then
         * let file pwritev handle it. */
        struct iovec* buffersv = malloc(iovlen * sizeof(*iov));

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            buffersv[i].iov_base = (void*)process_getReadablePtr(sys->process, bufPtr, bufSize);
            buffersv[i].iov_len = bufSize;
        }

#ifdef SYS_pwritev2
        result = regularfile_pwritev2((RegularFile*)desc, buffersv, iovlen, offset, flags);
#else
        if (flags) {
            warning("Ignoring flags");
        }
        result = regularfile_pwritev((RegularFile*)desc, buffersv, iovlen, offset);
#endif

        free(buffersv);
    } else {
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
                case DT_FILE: {
                    /* Handled above. */
                    utility_debugAssert(0);
                    break;
                }
                case DT_TCPSOCKET:
                case DT_UDPSOCKET: {
                    SysCallReturn scr = _syscallhandler_sendtoHelper(
                        sys, fd, bufPtr, bufSize, 0, (PluginPtr){0}, 0);

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
                    warning(
                        "writev() not yet implemented for descriptor type %i",
                        (int)dType);
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
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64,
                                       args->args[1].as_ptr,
                                       args->args[2].as_u64, 0, 0, 0);
}

SysCallReturn syscallhandler_preadv(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_readvHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_u64, args->args[4].as_u64, 0);
}

SysCallReturn syscallhandler_preadv2(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_readvHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_u64, args->args[4].as_u64, args->args[5].as_i64);
}

SysCallReturn syscallhandler_writev(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64,
                                        args->args[1].as_ptr,
                                        args->args[2].as_u64, 0, 0, 0);
}

SysCallReturn syscallhandler_pwritev(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_writevHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_u64, args->args[4].as_u64, 0);
}

SysCallReturn syscallhandler_pwritev2(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    return _syscallhandler_writevHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_u64, args->args[4].as_u64, args->args[5].as_i64);
}
