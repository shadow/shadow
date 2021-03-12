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
#include "main/host/descriptor/udp.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_ioctlFileHelper(SysCallHandler* sys, File* file, int fd,
                                           unsigned long request, PluginPtr argPtr) {
    int result = 0;

    // TODO: we should call file_ioctl() here, but depending on the request we may need to
    // copy in the request params first before passing them.
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

static int _syscallhandler_ioctlTCPHelper(SysCallHandler* sys, TCP* tcp, int fd,
                                          unsigned long request, PluginPtr argPtr) {
    int result = -EINVAL;
    size_t buflen = 0;

    switch (request) {
        case SIOCINQ: { // equivalent to FIONREAD
            buflen = tcp_getInputBufferLength(tcp);
            result = 0;
            break;
        }

        case SIOCOUTQ: { // equivalent to TIOCOUTQ
            buflen = tcp_getOutputBufferLength(tcp);
            result = 0;
            break;
        }

        case SIOCOUTQNSD: {
            buflen = tcp_getNotSentBytes(tcp);
            result = 0;
            break;
        }

        default: {
            result = -EINVAL;
            warning("We do not yet handle ioctl request %lu on tcp socket %i", request, fd);
            break;
        }
    }

    if (result == 0) {
        int* lenout = process_getWriteablePtr(sys->process, sys->thread, argPtr, sizeof(int));
        *lenout = (int)buflen;
    }

    return result;
}

static int _syscallhandler_ioctlUDPHelper(SysCallHandler* sys, UDP* udp, int fd,
                                          unsigned long request, PluginPtr argPtr) {
    int result = -EINVAL;
    size_t buflen = 0;

    switch (request) {
        case SIOCINQ: { // equivalent to FIONREAD
            buflen = socket_getInputBufferLength((Socket*)udp);
            result = 0;
            break;
        }

        case SIOCOUTQ: { // equivalent to TIOCOUTQ
            buflen = socket_getOutputBufferLength((Socket*)udp);
            result = 0;
            break;
        }

        default: {
            result = -EINVAL;
            warning("We do not yet handle ioctl request %lu on udp socket %i", request, fd);
            break;
        }
    }

    if (result == 0) {
        int* lenout = process_getWriteablePtr(sys->process, sys->thread, argPtr, sizeof(int));
        *lenout = (int)buflen;
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

    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, fd);
    int errcode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    LegacyDescriptorType dtype = descriptor_getType(desc);

    int result = 0;
    if (dtype == DT_FILE) {
        result = _syscallhandler_ioctlFileHelper(sys, (File*)desc, fd, request, argPtr);
    } else if (dtype == DT_TCPSOCKET) {
        result = _syscallhandler_ioctlTCPHelper(sys, (TCP*)desc, fd, request, argPtr);
    } else if (dtype == DT_UDPSOCKET) {
        result = _syscallhandler_ioctlUDPHelper(sys, (UDP*)desc, fd, request, argPtr);
    } else {
        warning("We do not support ioctl request %lu on descriptor %i", request, fd);
        result = -ENOTTY;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = result};
}
