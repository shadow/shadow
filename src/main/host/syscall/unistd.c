/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/unistd.h"

#include <errno.h>
#include <stdio.h>
#include <sys/utsname.h>

#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/eventd.h"
#include "main/host/descriptor/file.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/socket.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static SysCallReturn _syscallhandler_pipeHelper(SysCallHandler* sys,
                                                PluginPtr pipefdPtr,
                                                gint flags) {
    if (flags & O_DIRECT) {
        warning("We don't currently support pipes in 'O_DIRECT' mode.");
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -ENOTSUP};
    }

    /* Make sure they didn't pass a NULL pointer. */
    if (!pipefdPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* A pipe descriptor is simulated with our Channel object, where
     * one side is readonly, the other is writeonly. */
    Channel* pipeReader = channel_new(CT_READONLY, DT_PIPE);
    Channel* pipeWriter = channel_new(CT_WRITEONLY, DT_PIPE);
    channel_setLinkedChannel(pipeReader, pipeWriter);
    channel_setLinkedChannel(pipeWriter, pipeReader);

    /* Set any options that were given. */
    if (flags & O_NONBLOCK) {
        descriptor_addFlags((LegacyDescriptor*)pipeReader, O_NONBLOCK);
        descriptor_addFlags((LegacyDescriptor*)pipeWriter, O_NONBLOCK);
    }
    if (flags & O_CLOEXEC) {
        descriptor_addFlags((LegacyDescriptor*)pipeReader, O_CLOEXEC);
        descriptor_addFlags((LegacyDescriptor*)pipeWriter, O_CLOEXEC);
    }

    /* Return the pipe fds to the caller. */
    size_t sizeNeeded = sizeof(int) * 2;
    gint* pipefd = process_getWriteablePtr(sys->process, sys->thread, pipefdPtr, sizeNeeded);

    pipefd[0] =
        process_registerLegacyDescriptor(sys->process, (LegacyDescriptor*)pipeReader);
    pipefd[1] =
        process_registerLegacyDescriptor(sys->process, (LegacyDescriptor*)pipeWriter);

    debug("Created pipe reader fd %i and writer fd %i", pipefd[0], pipefd[1]);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

static SysCallReturn _syscallhandler_readHelper(SysCallHandler* sys, int fd,
                                                PluginPtr bufPtr,
                                                size_t bufSize, off_t offset) {
    debug(
        "trying to read %zu bytes on fd %i at offset %li", bufSize, fd, offset);

    /* Get the descriptor. */
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, fd);
    if (!desc) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EBADF};
    }

    /* Some logic depends on the descriptor type. */
    LegacyDescriptorType dType = descriptor_getType(desc);

    /* We can only seek on files, otherwise its a pipe error. */
    if (dType != DT_FILE && offset != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESPIPE};
    }

    /* Divert io on sockets to socket handler to pick up special checks. */
    if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
        return _syscallhandler_recvfromHelper(
            sys, fd, bufPtr, bufSize, 0, (PluginPtr){0}, (PluginPtr){0});
    }

    /* Now it's an error if the descriptor is closed. */
    int errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(desc);

    /* Need a non-null buffer. */
    if (!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Need a non-zero size. */
    if (!bufSize) {
        info("Invalid length %zu provided on descriptor %i", bufSize, fd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
    void* buf = process_getWriteablePtr(sys->process, sys->thread, bufPtr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_FILE:
            if (offset == 0) {
                result = file_read((File*)desc, buf, sizeNeeded);
            } else {
                result = file_pread((File*)desc, buf, sizeNeeded, offset);
            }
            break;
        case DT_EVENTD: result = eventd_read((EventD*)desc, buf, sizeNeeded); break;
        case DT_TIMER:
            result = timer_read((Timer*)desc, buf, sizeNeeded);
            break;
        case DT_PIPE:
            result = transport_receiveUserData(
                (Transport*)desc, buf, sizeNeeded, NULL, NULL);
            break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
            // We already diverted these to the socket handler above.
            utility_assert(0);
            break;
        case DT_UNIXSOCKET:
        case DT_EPOLL:
        default:
            warning("write() not yet implemented for descriptor type %i",
                    (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            critical("Indefinitely blocking a read of %zu bytes on file %i at "
                     "offset %li",
                     bufSize, fd, offset);
        }

        /* We need to block until the descriptor is ready to read. */
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_READABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)result};
}

static SysCallReturn _syscallhandler_writeHelper(SysCallHandler* sys, int fd,
                                                 PluginPtr bufPtr,
                                                 size_t bufSize, off_t offset) {
    debug("trying to write %zu bytes on fd %i at offset %li", bufSize, fd,
          offset);

    /* Get the descriptor. */
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, fd);
    if (!desc) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EBADF};
    }

    /* Some logic depends on the descriptor type. */
    LegacyDescriptorType dType = descriptor_getType(desc);

    /* We can only seek on files, otherwise its a pipe error. */
    if (dType != DT_FILE && offset != 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ESPIPE};
    }

    /* Divert io on sockets to socket handler to pick up special checks. */
    if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
        return _syscallhandler_sendtoHelper(
            sys, fd, bufPtr, bufSize, 0, (PluginPtr){0}, 0);
    }

    /* Now it's an error if the descriptor is closed. */
    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(desc);

    /* Need a non-null buffer. */
    if (!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
    const void* buf = process_getReadablePtr(sys->process, sys->thread, bufPtr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_FILE:
            if (offset == 0) {
                result = file_write((File*)desc, buf, sizeNeeded);
            } else {
                result = file_pwrite((File*)desc, buf, sizeNeeded, offset);
            }
            break;
        case DT_EVENTD: result = eventd_write((EventD*)desc, buf, sizeNeeded); break;
        case DT_TIMER: result = -EINVAL; break;
        case DT_PIPE:
            result =
                transport_sendUserData((Transport*)desc, buf, sizeNeeded, 0, 0);
            break;
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
            // We already diverted these to the socket handler above.
            utility_assert(0);
            break;
        case DT_UNIXSOCKET:
        case DT_EPOLL:
        default:
            warning("write(%d) not yet implemented for descriptor type %i", fd, (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* Blocking for file io will lock up the plugin because we don't
         * yet have a way to wait on file descriptors. */
        if (dType == DT_FILE) {
            critical("Indefinitely blocking a write of %zu bytes on file %i at "
                     "offset %li",
                     bufSize, fd, offset);
        }

        /* We need to block until the descriptor is ready to write. */
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_WRITABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)result};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_close(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    gint fd = args->args[0].as_i64;
    gint errorCode = 0;

    debug("Trying to close fd %i", fd);

    /* Check that fd is within bounds. */
    if (fd <= 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EBADF};
    }

    /* Check if this is a virtual Shadow descriptor. */
    LegacyDescriptor* descriptor = process_getRegisteredLegacyDescriptor(sys->process, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    if (descriptor && !errorCode) {
        debug("Closing descriptor %i", descriptor_getHandle(descriptor));
        descriptor_close(descriptor);
        return (SysCallReturn){.state = SYSCALL_DONE};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_dup(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    warning("Cannot dup legacy descriptors");
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EOPNOTSUPP};
}

SysCallReturn syscallhandler_pipe2(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(
        sys, args->args[0].as_ptr, args->args[1].as_i64);
}

SysCallReturn syscallhandler_pipe(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(sys, args->args[0].as_ptr, 0);
}

SysCallReturn syscallhandler_read(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    return _syscallhandler_readHelper(sys, args->args[0].as_i64,
                                      args->args[1].as_ptr,
                                      args->args[2].as_u64, 0);
}

SysCallReturn syscallhandler_pread64(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_readHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_i64);
}

SysCallReturn syscallhandler_write(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    return _syscallhandler_writeHelper(sys, args->args[0].as_i64,
                                       args->args[1].as_ptr,
                                       args->args[2].as_u64, 0);
}

SysCallReturn syscallhandler_pwrite64(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    return _syscallhandler_writeHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_i64);
}

SysCallReturn syscallhandler_getpid(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    // We can't handle this natively in the plugin if we want determinism
    guint pid = process_getProcessID(sys->process);
    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)pid};
}

SysCallReturn syscallhandler_set_tid_address(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr tidptr = args->args[0].as_ptr; // int*
    thread_setTidAddress(sys->thread, tidptr);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = thread_getID(sys->thread)};
}

SysCallReturn syscallhandler_uname(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    struct utsname* buf = NULL;

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[0].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    buf = process_getWriteablePtr(sys->process, sys->thread, args->args[0].as_ptr, sizeof(*buf));

    const gchar* hostname = host_getName(sys->host);

    snprintf(buf->sysname, _UTSNAME_SYSNAME_LENGTH, "shadowsys");
    snprintf(buf->nodename, _UTSNAME_NODENAME_LENGTH, "%s", hostname);
    snprintf(buf->release, _UTSNAME_RELEASE_LENGTH, "shadowrelease");
    snprintf(buf->version, _UTSNAME_VERSION_LENGTH, "shadowversion");
    snprintf(buf->machine, _UTSNAME_MACHINE_LENGTH, "shadowmachine");

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}
