/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/ioctl.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/ioctl.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_ioctlHelper(SysCallHandler* sys, File* file, int fd,
                                       unsigned long request,
                                       PluginPtr argPtr) {
    int result = 0;

    switch (request) {
        default: {
            result = -EINVAL;
            warning("We do not yet handle ioctl request %lu on file %i",
                    request, fd);
            break;
        }
    }

    return result;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_ioctl(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    unsigned long request = args->args[1].as_i64;
    PluginPtr argPtr = args->args[2].as_ptr; // type depends on request

    debug("ioctl called on fd %d for request %ld", fd, request);

    Descriptor* desc = process_getRegisteredDescriptor(sys->process, fd);
    int errcode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    bool isInbufLenRequest = request == SIOCINQ || request == FIONREAD;
    bool isOutbufLenRequest = request == SIOCOUTQ || request == TIOCOUTQ;

    DescriptorType dtype = descriptor_getType(desc);

    int result = 0;
    if (dtype == DT_FILE) {
        result =
            _syscallhandler_ioctlHelper(sys, (File*)desc, fd, request, argPtr);
    } else if ((dtype == DT_TCPSOCKET || dtype == DT_UDPSOCKET) &&
               (isInbufLenRequest || isOutbufLenRequest)) {
        size_t buflen = 0;

        if (dtype == DT_TCPSOCKET) {
            if (isInbufLenRequest) {
                buflen = tcp_getInputBufferLength((TCP*)desc);
            } else {
                buflen = tcp_getOutputBufferLength((TCP*)desc);
            }
        } else {
            if (isInbufLenRequest) {
                buflen = socket_getInputBufferLength((Socket*)desc);
            } else {
                buflen = socket_getOutputBufferLength((Socket*)desc);
            }
        }

        int* lenout = thread_getWriteablePtr(sys->thread, argPtr, sizeof(int));
        *lenout = (int)buflen;
    } else {
        warning("We do not support ioctl request %lu on descriptor %i", request,
                fd);
        result = -ENOTTY;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}
