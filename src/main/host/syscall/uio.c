/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/uio.h"

#include <errno.h>
#include <sys/syscall.h>
#include <sys/uio.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateVecParams(SysCallHandler* sys, int fd,
                                             PluginPtr iovPtr,
                                             unsigned long iovlen, off_t offset,
                                             LegacyDescriptor** desc_out,
                                             const struct iovec** iov_out) {
    /* Get the descriptor. */
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, fd);
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
    if (descriptor_getType(desc) != DT_FILE && offset != 0) {
        return -ESPIPE;
    }

    /* Get the vector of pointers. */
    const struct iovec* iov =
        process_getReadablePtr(sys->process, sys->thread, iovPtr, iovlen * sizeof(*iov));

    /* Check that all of the buf pointers are valid. */
    for (unsigned long i = 0; i < iovlen; i++) {
        PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
        size_t bufSize = iov[i].iov_len;

        if (!bufPtr.val) {
            info("Invalid NULL pointer in iovec[%ld]", i);
            return -EFAULT;
        }

        if (!bufSize) {
            info("Invalid size 0 in iovec[%ld]", i);
            return -EINVAL;
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

    debug("Trying to readv from fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    LegacyDescriptor* desc = NULL;
    const struct iovec* iov;
    int errcode = _syscallhandler_validateVecParams(
        sys, fd, iovPtr, iovlen, offset, &desc, &iov);
    if (errcode < 0 || iovlen == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Some logic depends on the descriptor type. */
    LegacyDescriptorType dType = descriptor_getType(desc);

    ssize_t result = 0;

    /* Now we can perform the write operations. */
    if (dType == DT_FILE) {
        /* For files, we read all of the buffers from the plugin and then
         * let file pwritev handle it. */
        struct iovec* buffersv = malloc(iovlen * sizeof(*iov));

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            buffersv[i].iov_base =
                process_getWriteablePtr(sys->process, sys->thread, bufPtr, bufSize);
            buffersv[i].iov_len = bufSize;
        }

#ifdef SYS_preadv2
        result = file_preadv2((File*)desc, buffersv, iovlen, offset, flags);
#else
        if (flags) {
            warning("Ignoring flags");
        }
        result = file_preadv((File*)desc, buffersv, iovlen, offset);
#endif

        free(buffersv);
    } else {
        /* For non-files, we only read one buffer at a time to avoid
         * unnecessary data transfer between the plugin and Shadow. */
        size_t totalBytesWritten = 0;

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            switch (dType) {
                case DT_FILE: {
                    /* Handled above. */
                    utility_assert(0);
                    break;
                }
                case DT_PIPE: {
                    void* buf = process_getWriteablePtr(sys->process, sys->thread, bufPtr, bufSize);
                    result = transport_receiveUserData(
                        (Transport*)desc, buf, bufSize, NULL, NULL);
                    break;
                }
                case DT_TCPSOCKET:
                case DT_UDPSOCKET: {
                    SysCallReturn scr = _syscallhandler_recvfromHelper(
                        sys, fd, bufPtr, bufSize, 0, (PluginPtr){0},
                        (PluginPtr){0});
                    result = scr.retval.as_i64;
                    break;
                }
                case DT_TIMER:
                case DT_UNIXSOCKET:
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
                if (result == -EWOULDBLOCK && totalBytesWritten > 0) {
                    result = totalBytesWritten;
                }
                break;
            }
        }
    }

    if (result == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            critical("Indefinitely blocking a readv of vector length %lu on "
                     "file %i at offset %li",
                     iovlen, fd, offset);
        }

        /* We need to block until the descriptor is ready to write. */
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_READABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}

static SysCallReturn
_syscallhandler_writevHelper(SysCallHandler* sys, int fd, PluginPtr iovPtr,
                             unsigned long iovlen, unsigned long pos_l,
                             unsigned long pos_h, int flags) {
    /* Reconstruct the offset from the high and low bits */
    off_t offset = (off_t)((pos_h << 32) & pos_l);

    debug("Trying to writev to fd %d, ptr %p, size %zu, pos_l %lu, pos_h %lu, "
          "offset %ld, flags %d",
          fd, (void*)iovPtr.val, iovlen, pos_l, pos_h, offset, flags);

    LegacyDescriptor* desc = NULL;
    const struct iovec* iov;
    int errcode = _syscallhandler_validateVecParams(
        sys, fd, iovPtr, iovlen, offset, &desc, &iov);
    if (errcode < 0 || iovlen == 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Some logic depends on the descriptor type. */
    LegacyDescriptorType dType = descriptor_getType(desc);

    ssize_t result = 0;

    /* Now we can perform the write operations. */
    if (dType == DT_FILE) {
        /* For files, we read all of the buffers from the plugin and then
         * let file pwritev handle it. */
        struct iovec* buffersv = malloc(iovlen * sizeof(*iov));

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            buffersv[i].iov_base =
                (void*)process_getReadablePtr(sys->process, sys->thread, bufPtr, bufSize);
            buffersv[i].iov_len = bufSize;
        }

#ifdef SYS_pwritev2
        result = file_pwritev2((File*)desc, buffersv, iovlen, offset, flags);
#else
        if (flags) {
            warning("Ignoring flags");
        }
        result = file_pwritev((File*)desc, buffersv, iovlen, offset);
#endif

        free(buffersv);
    } else {
        /* For non-files, we only read one buffer at a time to avoid
         * unnecessary data transfer between the plugin and Shadow. */
        size_t totalBytesWritten = 0;

        for (unsigned long i = 0; i < iovlen; i++) {
            PluginPtr bufPtr = (PluginPtr){.val = (uint64_t)iov[i].iov_base};
            size_t bufSize = iov[i].iov_len;

            switch (dType) {
                case DT_FILE: {
                    /* Handled above. */
                    utility_assert(0);
                    break;
                }
                case DT_PIPE: {
                    const void* buf =
                        process_getReadablePtr(sys->process, sys->thread, bufPtr, bufSize);
                    result = transport_sendUserData(
                        (Transport*)desc, buf, bufSize, 0, 0);
                    break;
                }
                case DT_TCPSOCKET:
                case DT_UDPSOCKET: {
                    SysCallReturn scr = _syscallhandler_sendtoHelper(
                        sys, fd, bufPtr, bufSize, 0, (PluginPtr){0}, 0);
                    result = scr.retval.as_i64;
                    break;
                }
                case DT_TIMER:
                case DT_UNIXSOCKET:
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
                if (result == -EWOULDBLOCK && totalBytesWritten > 0) {
                    result = totalBytesWritten;
                }
                break;
            }
        }
    }

    if (result == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            critical("Indefinitely blocking a writev of vector length %lu on "
                     "file %i at offset %li",
                     iovlen, fd, offset);
        }

        /* We need to block until the descriptor is ready to write. */
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_WRITABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
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
