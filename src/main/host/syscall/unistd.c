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
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
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

    /* Create and check the pipe descriptor. */
    Descriptor* pipeReader = host_createDescriptor(sys->host, DT_PIPE);
    utility_assert(pipeReader);
    gint errorCode = _syscallhandler_validateDescriptor(pipeReader, DT_PIPE);
    utility_assert(errorCode == 0);

    /* A pipe descriptor is actually simulated with our Channel object,
     * the other end of which will represent the write end. */
    Descriptor* pipeWriter =
        (Descriptor*)channel_getLinkedChannel((Channel*)pipeReader);
    utility_assert(pipeWriter);
    errorCode = _syscallhandler_validateDescriptor(pipeWriter, DT_PIPE);
    utility_assert(errorCode == 0);

    /* Set any options that were given. */
    if (flags & O_NONBLOCK) {
        descriptor_addFlags(pipeReader, O_NONBLOCK);
        descriptor_addFlags(pipeWriter, O_NONBLOCK);
    }
    if (flags & O_CLOEXEC) {
        descriptor_addFlags(pipeReader, O_CLOEXEC);
        descriptor_addFlags(pipeWriter, O_CLOEXEC);
    }

    /* Return the pipe fds to the caller. */
    size_t sizeNeeded = sizeof(int) * 2;
    gint* pipefd = thread_getWriteablePtr(sys->thread, pipefdPtr, sizeNeeded);
    pipefd[0] = descriptor_getHandle(pipeReader);
    pipefd[1] = descriptor_getHandle(pipeWriter);

    debug("pipe() returning reader fd %i and writer fd %i",
          descriptor_getHandle(pipeReader), descriptor_getHandle(pipeWriter));

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_close(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    gint fd = (gint)args->args[0].as_i64;
    gint errorCode = 0;

    /* Check that fd is within bounds. */
    if (fd <= 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EBADF};
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* descriptor = host_lookupDescriptor(sys->host, fd);
    errorCode = _syscallhandler_validateDescriptor(descriptor, DT_NONE);

    if (descriptor && !errorCode) {
        descriptor_close(descriptor);
        return (SysCallReturn){.state = SYSCALL_DONE};
    }

    /* Check if we have a mapped os fd. This call returns -1 to
     * us if this fd does not correspond to any os-backed file
     * that Shadow created internally. */
    gint osfd = host_getOSHandle(sys->host, fd);
    if (osfd < 0) {
        /* The fd is not part of a special file that Shadow handles internally.
         * It might be a regular OS file, and should be handled natively by
         * libc. */
        return (SysCallReturn){.state = SYSCALL_NATIVE};
    }

    /* OK. The given FD from the plugin corresponds to a real
     * OS file that Shadow created and handles. */

    // TODO: handle special files

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_pipe2(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(
        sys, args->args[0].as_ptr, (gint)args->args[1].as_i64);
}

SysCallReturn syscallhandler_pipe(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    return _syscallhandler_pipeHelper(sys, args->args[0].as_ptr, 0);
}

SysCallReturn syscallhandler_read(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    int fd = (int)args->args[0].as_i64;
    void* buf; // args->args[1]
    size_t bufSize = (size_t)args->args[2].as_u64;

    debug("trying to read %zu bytes on fd %i", bufSize, fd);

    /* Get the descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, fd);

    // TODO: I think every read/write on FDs needs to come through shadow.
    // The following needs to change when we add file support.
    if (!desc) {
        return (SysCallReturn){.state = SYSCALL_NATIVE, .retval.as_i64 = 0};
    }

    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);

    if (errorCode < 0 && _syscallhandler_readableWhenClosed(sys, desc)) {
        errorCode = 0;
    }

    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(desc);

    DescriptorType dType = descriptor_getType(desc);
    gint dFlags = descriptor_getFlags(desc);

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[1].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    if (!bufSize) {
        info("Invalid length %zu provided on descriptor %i", bufSize, fd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
    buf = thread_getWriteablePtr(sys->thread, args->args[1].as_ptr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_TIMER:
            result = timer_read((Timer*)desc, buf, sizeNeeded);
            break;
        case DT_PIPE:
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
            /* TODO: is read() on a socket identical to recvfrom()? If so, then
             * we should probably redirect to the socket syscall handler as
             * soon as we know we have a non-NULL desc of type socket to pick
             * up the error checks etc. */
            result = transport_receiveUserData(
                (Transport*)desc, buf, sizeNeeded, NULL, NULL);
            break;
        case DT_SOCKETPAIR:
        case DT_EPOLL:
        default:
            warning("write() not yet implemented for descriptor type %i",
                    (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(dFlags & O_NONBLOCK)) {
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_READABLE);
        return (SysCallReturn){.state = SYSCALL_BLOCK};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)result};
}

SysCallReturn syscallhandler_write(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int fd = (int)args->args[0].as_i64;
    const void* buf; // args->args[1]
    size_t bufSize = (size_t)args->args[2].as_u64;

    debug("trying to write %zu bytes on fd %i", bufSize, fd);

    /* Get the descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, fd);

    // TODO: I think every read/write on FDs needs to come through shadow.
    // The following needs to change when we add file support.
    if (!desc) {
        return (SysCallReturn){.state = SYSCALL_NATIVE, .retval.as_i64 = 0};
    }

    gint errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errorCode != 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(desc);

    DescriptorType dType = descriptor_getType(desc);
    gint dFlags = descriptor_getFlags(desc);

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[1].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    if (!bufSize) {
        info("Invalid length %zu provided on descriptor %i", bufSize, fd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
    buf = thread_getReadablePtr(sys->thread, args->args[1].as_ptr, sizeNeeded);

    ssize_t result = 0;
    switch (dType) {
        case DT_TIMER: result = -EINVAL; break;
        case DT_PIPE:
        case DT_TCPSOCKET:
        case DT_UDPSOCKET:
            /* TODO: is write() on a socket identical to sendto()? If so, then
             * we should probably redirect to the socket syscall handler as
             * soon as we know we have a non-NULL desc of type socket to pick
             * up the error checks etc. */
            result =
                transport_sendUserData((Transport*)desc, buf, sizeNeeded, 0, 0);
            break;
        case DT_SOCKETPAIR:
        case DT_EPOLL:
        default:
            warning("write() not yet implemented for descriptor type %i",
                    (int)dType);
            result = -ENOTSUP;
            break;
    }

    if (result == -EWOULDBLOCK && !(dFlags & O_NONBLOCK)) {
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_WRITABLE);
        return (SysCallReturn){.state = SYSCALL_BLOCK};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)result};
}

SysCallReturn syscallhandler_getpid(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    // We can't handle this natively in the plugin if we want determinism
    guint pid = process_getProcessID(sys->process);
    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)pid};
}

SysCallReturn syscallhandler_uname(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    struct utsname* buf = NULL;

    /* Make sure they didn't pass a NULL pointer. */
    if (!args->args[0].as_ptr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    buf =
        thread_getWriteablePtr(sys->thread, args->args[0].as_ptr, sizeof(*buf));

    const gchar* hostname = host_getName(sys->host);

    snprintf(buf->sysname, _UTSNAME_SYSNAME_LENGTH, "shadowsys");
    snprintf(buf->nodename, _UTSNAME_NODENAME_LENGTH, "%s", hostname);
    snprintf(buf->release, _UTSNAME_RELEASE_LENGTH, "shadowrelease");
    snprintf(buf->version, _UTSNAME_VERSION_LENGTH, "shadowversion");
    snprintf(buf->machine, _UTSNAME_MACHINE_LENGTH, "shadowmachine");

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}
