/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/ioctl.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/ioctl.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/udp.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_ioctlFileHelper(SysCallHandler* sys, RegularFile* file, int fd,
                                           unsigned long request, PluginPtr argPtr) {
    int result = 0;

    // TODO: we should call regularfile_ioctl() here, but depending on the request we may need to
    // copy in the request params first before passing them.
    switch (request) {
        case TCGETS:
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
        case TCGETA:
        case TCSETA:
        case TCSETAW:
        case TCSETAF:
        case TIOCGWINSZ:
        case TIOCSWINSZ: {
            // not a terminal
            result = -ENOTTY;
            break;
        }

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

    switch (request) {
        case SIOCINQ: { // equivalent to FIONREAD
            int lenout = tcp_getInputBufferLength(tcp);
            int rv = process_writePtr(sys->process, argPtr, &lenout, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            result = 0;
            break;
        }

        case SIOCOUTQ: { // equivalent to TIOCOUTQ
            int lenout = tcp_getOutputBufferLength(tcp);
            int rv = process_writePtr(sys->process, argPtr, &lenout, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            result = 0;
            break;
        }

        case SIOCOUTQNSD: {
            int lenout = tcp_getNotSentBytes(tcp);
            int rv = process_writePtr(sys->process, argPtr, &lenout, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            result = 0;
            break;
        }

        case FIONBIO: {
            int val = 0;
            int rv = process_readPtr(sys->process, &val, argPtr, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            if (val == 0) {
                legacyfile_removeFlags((LegacyFile*)tcp, O_NONBLOCK);
            } else {
                legacyfile_addFlags((LegacyFile*)tcp, O_NONBLOCK);
            }

            result = 0;
            break;
        }

        case TCGETS:
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
        case TCGETA:
        case TCSETA:
        case TCSETAW:
        case TCSETAF:
        case TIOCGWINSZ:
        case TIOCSWINSZ: {
            // not a terminal
            result = -ENOTTY;
            break;
        }

        default: {
            result = -EINVAL;
            warning("We do not yet handle ioctl request %lu on tcp socket %i", request, fd);
            break;
        }
    }

    return result;
}

static int _syscallhandler_ioctlUDPHelper(SysCallHandler* sys, UDP* udp, int fd,
                                          unsigned long request, PluginPtr argPtr) {
    int result = -EINVAL;

    switch (request) {
        case SIOCINQ: { // equivalent to FIONREAD
            int lenout = legacysocket_getInputBufferLength((LegacySocket*)udp);
            int rv = process_writePtr(sys->process, argPtr, &lenout, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            result = 0;
            break;
        }

        case SIOCOUTQ: { // equivalent to TIOCOUTQ
            int lenout = legacysocket_getOutputBufferLength((LegacySocket*)udp);
            int rv = process_writePtr(sys->process, argPtr, &lenout, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            result = 0;
            break;
        }

        case FIONBIO: {
            int val = 0;
            int rv = process_readPtr(sys->process, &val, argPtr, sizeof(int));

            if (rv != 0) {
                utility_debugAssert(rv < 0);
                result = rv;
                break;
            }

            if (val == 0) {
                legacyfile_removeFlags((LegacyFile*)udp, O_NONBLOCK);
            } else {
                legacyfile_addFlags((LegacyFile*)udp, O_NONBLOCK);
            }

            result = 0;
            break;
        }

        case TCGETS:
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
        case TCGETA:
        case TCSETA:
        case TCSETAW:
        case TCSETAF:
        case TIOCGWINSZ:
        case TIOCSWINSZ: {
            // not a terminal
            result = -ENOTTY;
            break;
        }

        default: {
            result = -EINVAL;
            warning("We do not yet handle ioctl request %lu on udp socket %i", request, fd);
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

    trace("ioctl called on fd %d for request %ld", fd, request);

    LegacyFile* desc = process_getRegisteredLegacyFile(sys->process, fd);
    int errcode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    LegacyFileType dtype = legacyfile_getType(desc);

    int result = 0;
    if (dtype == DT_FILE) {
        result = _syscallhandler_ioctlFileHelper(sys, (RegularFile*)desc, fd, request, argPtr);
    } else if (dtype == DT_TCPSOCKET) {
        result = _syscallhandler_ioctlTCPHelper(sys, (TCP*)desc, fd, request, argPtr);
    } else if (dtype == DT_UDPSOCKET) {
        result = _syscallhandler_ioctlUDPHelper(sys, (UDP*)desc, fd, request, argPtr);
    } else {
        warning(
            "We do not support ioctl request %lu on descriptor %i of type %i", request, fd, dtype);
        result = -ENOTTY;
    }

    return syscallreturn_makeDoneI64(result);
}
