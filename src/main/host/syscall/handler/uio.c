/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/uio.h"

#include <assert.h>
#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/handler/unistd.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/syscall_condition.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateVecParams(SyscallHandler* sys, int fd, UntypedForeignPtr iovPtr,
                                             unsigned long iovlen, off_t offset,
                                             LegacyFile** desc_out, struct iovec** iov_out) {
    /* Get the descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), fd);
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
    if (process_readPtr(rustsyscallhandler_getProcess(sys), iov, iovPtr, iovlen * sizeof(*iov)) != 0) {
        warning("Got unreadable pointer [%p..+%zu]", (void*)iovPtr.val, iovlen * sizeof(*iov));
        free(iov);
        return -EFAULT;
    }

    /* Check that all of the buf pointers are valid. */
    for (unsigned long i = 0; i < iovlen; i++) {
        UntypedForeignPtr bufPtr = (UntypedForeignPtr){.val = (uint64_t)iov[i].iov_base};
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

static SyscallReturn _syscallhandler_readvHelper(SyscallHandler* sys, int fd,
                                                 UntypedForeignPtr iovPtr, unsigned long iovlen,
                                                 unsigned long pos_l, unsigned long pos_h,
                                                 int flags, bool doPreadv,
                                                 bool negativeOffsetDisables) {
    /* On Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `pos_h`. */
    static_assert(sizeof(unsigned long) == sizeof(off_t), "Unexpected `unsigned long` size");
    off_t offset = pos_l;

    /* If the offset is -1 for preadv2, disable the offset. */
    if (offset == -1 && negativeOffsetDisables) {
        offset = 0;
        doPreadv = false;
    }

    trace("Trying to readv from fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    if (offset < 0 && doPreadv) {
        return syscallreturn_makeDoneI64(-EINVAL);
    }

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
        UntypedForeignPtr bufPtr = (UntypedForeignPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (bufSize == 0) {
            /* Nothing to do if the buffer is empty. */
            continue;
        }

        switch (dType) {
            case DT_FILE: {
                off_t thisOffset = offset;

                if (doPreadv) {
                    thisOffset += totalBytesWritten;
                }

                SyscallReturn scr =
                    _syscallhandler_readHelper(sys, fd, bufPtr, bufSize, thisOffset, doPreadv);

                // if the above syscall handler created any pointers, we may
                // need to flush them before calling the syscall handler again
                result = process_flushPtrs(rustsyscallhandler_getProcess(sys));
                if (result != 0) {
                    break;
                }

                switch (scr.tag) {
                    case SYSCALL_RETURN_DONE: {
                        result = syscallreturn_done(&scr)->retval.as_i64;
                        break;
                    }
                    case SYSCALL_RETURN_BLOCK: {
                        // assume that there was no timer, and that we're blocked on this socket
                        SyscallReturnBlocked* blocked = syscallreturn_blocked(&scr);
                        syscallcondition_unref(blocked->cond);
                        result = -EWOULDBLOCK;
                        break;
                    }
                    case SYSCALL_RETURN_NATIVE: {
                        panic("recv() returned SYSCALL_NATIVE");
                    }
                }

                break;
            }
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
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = FileState_READABLE};

        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

static SyscallReturn _syscallhandler_writevHelper(SyscallHandler* sys, int fd,
                                                  UntypedForeignPtr iovPtr, unsigned long iovlen,
                                                  unsigned long pos_l, unsigned long pos_h,
                                                  int flags, bool doPwritev,
                                                  bool negativeOffsetDisables) {
    /* On Linux x86-64, an `unsigned long` is 64 bits, so we can ignore `pos_h`. */
    static_assert(sizeof(unsigned long) == sizeof(off_t), "Unexpected `unsigned long` size");
    off_t offset = pos_l;

    /* If the offset is -1 for pwritev2, disable the offset. */
    if (offset == -1 && negativeOffsetDisables) {
        offset = 0;
        doPwritev = false;
    }

    trace("Trying to writev to fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    if (offset < 0 && doPwritev) {
        return syscallreturn_makeDoneI64(-EINVAL);
    }

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
        UntypedForeignPtr bufPtr = (UntypedForeignPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (bufSize == 0) {
            /* Nothing to do if the buffer is empty. */
            continue;
        }

        switch (dType) {
            case DT_FILE: {
                off_t thisOffset = offset;

                if (doPwritev) {
                    thisOffset += totalBytesWritten;
                }

                SyscallReturn scr =
                    _syscallhandler_writeHelper(sys, fd, bufPtr, bufSize, thisOffset, doPwritev);

                // if the above syscall handler created any pointers, we may
                // need to flush them before calling the syscall handler again
                result = process_flushPtrs(rustsyscallhandler_getProcess(sys));
                if (result != 0) {
                    break;
                }

                switch (scr.tag) {
                    case SYSCALL_RETURN_DONE: {
                        result = syscallreturn_done(&scr)->retval.as_i64;
                        break;
                    }
                    case SYSCALL_RETURN_BLOCK: {
                        // assume that there was no timer, and that we're blocked on this socket
                        SyscallReturnBlocked* blocked = syscallreturn_blocked(&scr);
                        syscallcondition_unref(blocked->cond);
                        result = -EWOULDBLOCK;
                        break;
                    }
                    case SYSCALL_RETURN_NATIVE: {
                        panic("send() returned SYSCALL_NATIVE");
                    }
                }

                break;
            }
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
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = FileState_WRITABLE};

        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    return syscallreturn_makeDoneI64(result);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_readv(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, 0, 0, 0, false, false);
}

SyscallReturn syscallhandler_preadv(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_u64,
                                       args->args[4].as_u64, 0, true, false);
}

SyscallReturn syscallhandler_preadv2(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_readvHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                       args->args[2].as_u64, args->args[3].as_u64,
                                       args->args[4].as_u64, args->args[5].as_i64, true, true);
}

SyscallReturn syscallhandler_writev(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                        args->args[2].as_u64, 0, 0, 0, false, false);
}

SyscallReturn syscallhandler_pwritev(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                        args->args[2].as_u64, args->args[3].as_u64,
                                        args->args[4].as_u64, 0, true, false);
}

SyscallReturn syscallhandler_pwritev2(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_writevHelper(sys, args->args[0].as_i64, args->args[1].as_ptr,
                                        args->args[2].as_u64, args->args[3].as_u64,
                                        args->args[4].as_u64, args->args[5].as_i64, true, true);
}
