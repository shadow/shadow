/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/socket.h"

#include <errno.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/tcp_cong.h"
#include "main/host/descriptor/tcp_cong_reno.h"
#include "main/host/descriptor/udp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread.h"

///////////////////////////////////////////////////////////
// Private Helpers
///////////////////////////////////////////////////////////

/* It's valid to read data from a socket even if close() was already called,
 * as long as the EOF has not yet been read (i.e., there is still data that
 * must be read from the socket). This function checks if the descriptor is
 * in this corner case and we should be allowed to read from it. */
static bool _syscallhandler_readableWhenClosed(SysCallHandler* sys, LegacyFile* desc) {
    if (desc && legacyfile_getType(desc) == DT_TCPSOCKET &&
        (legacyfile_getStatus(desc) & STATUS_FILE_CLOSED)) {
        /* Connection error will be -ENOTCONN when reading is done. */
        if (tcp_getConnectionError((TCP*)desc) == -EISCONN) {
            return true;
        }
    }
    return false;
}

static int _syscallhandler_validateSocketHelper(SysCallHandler* sys, int sockfd,
                                                LegacySocket** sock_desc_out) {
    /* Check that fd is within bounds. */
    if (sockfd < 0) {
        debug("descriptor %i out of bounds", sockfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    LegacyFile* desc = process_getRegisteredLegacyFile(sys->process, sockfd);
    if (desc && sock_desc_out) {
        *sock_desc_out = (LegacySocket*)desc;
    }

    int errcode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
    if (errcode) {
        debug("descriptor %i is invalid", sockfd);
        return errcode;
    }

    LegacyFileType type = legacyfile_getType(desc);
    if (type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
        debug("descriptor %i with type %i is not a socket", sockfd, (int)type);
        return -ENOTSOCK;
    }

    /* Now we know we have a valid socket. */
    return 0;
}

static int _syscallhandler_validateTCPSocketHelper(SysCallHandler* sys,
                                                   int sockfd,
                                                   TCP** tcp_desc_out) {
    LegacySocket* sock_desc = NULL;
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, &sock_desc);

    if (sock_desc && tcp_desc_out) {
        *tcp_desc_out = (TCP*)sock_desc;
    }
    if (errcode) {
        return errcode;
    }

    LegacyFileType type = legacyfile_getType((LegacyFile*)sock_desc);
    if (type != DT_TCPSOCKET) {
        debug("descriptor %i is not a TCP socket", sockfd);
        return -EOPNOTSUPP;
    }

    /* Now we know we have a valid TCP socket. */
    return 0;
}

static int _syscallhandler_validateUDPSocketHelper(SysCallHandler* sys,
                                                   int sockfd,
                                                   UDP** udp_desc_out) {
    LegacySocket* sock_desc = NULL;
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, &sock_desc);

    if (sock_desc && udp_desc_out) {
        *udp_desc_out = (UDP*)sock_desc;
    }
    if (errcode) {
        return errcode;
    }

    LegacyFileType type = legacyfile_getType((LegacyFile*)sock_desc);
    if (type != DT_UDPSOCKET) {
        debug("descriptor %i is not a UDP socket", sockfd);
        return -EOPNOTSUPP;
    }

    /* Now we know we have a valid UDP socket. */
    return 0;
}

static int _syscallhandler_getnameHelper(SysCallHandler* sys, struct sockaddr* saddr, size_t slen,
                                         PluginPtr addrPtr, PluginPtr addrlenPtr) {
    socklen_t addrlen;
    if (process_readPtr(sys->process, &addrlen, addrlenPtr, sizeof(addrlen)) != 0) {
        debug("Couldn't read addrlenPtr %p", (void*)addrlenPtr.val);
        return -EFAULT;
    }

    /* The result is truncated if they didn't give us enough space. */
    size_t retSize = MIN(addrlen, slen);

    addrlen = slen;
    if (process_writePtr(sys->process, addrlenPtr, &addrlen, sizeof(addrlen)) != 0) {
        debug("Couldn't write addrlenPtr %p", (void*)addrlenPtr.val);
        return -EFAULT;
    }

    if (retSize > 0) {
        /* Return the results */
        if (process_writePtr(sys->process, addrPtr, saddr, retSize) != 0) {
            debug("Couldn't write addrPtr %p", (void*)addrPtr.val);
            return -EFAULT;
        }
    }

    return 0;
}

static SysCallReturn _syscallhandler_acceptHelper(SysCallHandler* sys,
                                                  int sockfd, PluginPtr addrPtr,
                                                  PluginPtr addrlenPtr,
                                                  int flags) {
    trace("trying to accept on socket %i", sockfd);

    /* Check that non-valid flags are not given. */
    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
        debug("invalid flags \"%i\", only SOCK_NONBLOCK and SOCK_CLOEXEC are "
              "allowed",
              flags);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errcode =
        _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);

    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(tcp_desc);

    /* We must be listening in order to accept. */
    if (!tcp_isValidListener(tcp_desc)) {
        debug("socket %i is not listening", sockfd);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* OK, now we can check if we have anything to accept. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    int accepted_fd = 0;
    errcode = tcp_acceptServerPeer(tcp_desc, _syscallhandler_getHost(sys),
                                   &inet_addr.sin_addr.s_addr, &inet_addr.sin_port, &accepted_fd);

    LegacyFile* legacyDesc = (LegacyFile*)tcp_desc;
    if (errcode == -EWOULDBLOCK && !(legacyfile_getFlags(legacyDesc) & O_NONBLOCK)) {
        /* This is a blocking accept, and we don't have a connection yet.
         * The socket becomes readable when we have a connection to accept.
         * This blocks indefinitely without a timeout. */
        trace("Listening socket %i waiting for acceptable connection.", sockfd);
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = legacyDesc, .status = STATUS_FILE_READABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(legacyDesc));
    } else if (errcode < 0) {
        trace("TCP error when accepting connection on socket %i", sockfd);
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* We accepted something! */
    utility_debugAssert(accepted_fd > 0);
    TCP* accepted_tcp_desc = NULL;
    errcode = _syscallhandler_validateTCPSocketHelper(
        sys, accepted_fd, &accepted_tcp_desc);
    utility_debugAssert(errcode == 0);

    trace("listening socket %i accepted fd %i", sockfd, accepted_fd);

    /* Get the descriptor for this new socket and set flags if necessary. */
    Descriptor* desc = process_getRegisteredDescriptorMut(sys->process, accepted_fd);
    if (flags & SOCK_CLOEXEC) {
        descriptor_setFlags(desc, O_CLOEXEC);
    }
    desc = NULL;

    /* Set the flags on the accepted socket if requested. */
    if (flags & SOCK_NONBLOCK) {
        legacyfile_addFlags((LegacyFile*)accepted_tcp_desc, O_NONBLOCK);
    }

    /* check if they wanted to know where we got the data from */
    if (addrPtr.val) {
        errcode = _syscallhandler_getnameHelper(
            sys, (struct sockaddr*)&inet_addr, sizeof(inet_addr), addrPtr, addrlenPtr);
        if (errcode != 0) {
            return syscallreturn_makeDoneErrno(-errcode);
        }
    }

    return syscallreturn_makeDoneI64(accepted_fd);
}

static int _syscallhandler_bindHelper(SysCallHandler* sys, LegacySocket* socket_desc,
                                      in_addr_t addr, in_port_t port, in_addr_t peerAddr,
                                      in_port_t peerPort) {
#ifdef DEBUG
    gchar* bindAddrStr = address_ipToNewString(addr);
    gchar* peerAddrStr = address_ipToNewString(peerAddr);
    trace("trying to bind to inet address %s:%u on socket %p with peer %s:%u", bindAddrStr,
          ntohs(port), (LegacyFile*)socket_desc, peerAddrStr, ntohs(peerPort));
    g_free(bindAddrStr);
    g_free(peerAddrStr);
#endif

    /* make sure we have an interface at that address */
    if (!host_doesInterfaceExist(_syscallhandler_getHost(sys), addr)) {
        debug("no network interface exists for the provided bind address");
        return -EINVAL;
    }

    /* Each protocol type gets its own ephemeral port mapping. */
    ProtocolType ptype = legacysocket_getProtocol(socket_desc);

    /* Get a free ephemeral port if they didn't specify one. */
    if (port == 0) {
        port =
            host_getRandomFreePort(_syscallhandler_getHost(sys), ptype, addr, peerAddr, peerPort);
        trace("binding to generated ephemeral port %u", ntohs(port));
    }

    /* Ephemeral port unavailable. */
    if (port == 0) {
        debug("binding required an ephemeral port and none are available");
        return -EADDRINUSE;
    }

    /* Make sure the port is available at this address for this protocol. */
    if (!host_isInterfaceAvailable(
            _syscallhandler_getHost(sys), ptype, addr, port, peerAddr, peerPort)) {
        debug("the provided address and port %u are not available", ntohs(port));
        return -EADDRINUSE;
    }

    /* connect up socket layer */
    legacysocket_setPeerName(socket_desc, peerAddr, peerPort);
    legacysocket_setSocketName(socket_desc, addr, port);

    /* set associations */
    CompatSocket compat_socket = compatsocket_fromLegacySocket(socket_desc);
    host_associateInterface(_syscallhandler_getHost(sys), &compat_socket, addr);
    return 0;
}

static int _syscallhandler_getTCPOptHelper(SysCallHandler* sys, TCP* tcp, int optname, void* optval,
                                           socklen_t* optlen) {
    switch (optname) {
        case TCP_INFO: {
            struct tcp_info info;
            tcp_getInfo(tcp, &info);

            int num_bytes = MIN(*optlen, sizeof(info));
            memcpy(optval, &info, num_bytes);
            *optlen = num_bytes;

            return 0;
        }
        case TCP_NODELAY: {
            /* Shadow doesn't support nagle's algorithm, so shadow always behaves
             * as if TCP_NODELAY is enabled.
             */
            int val = 1;
            int num_bytes = MIN(*optlen, sizeof(int));
            memcpy(optval, &val, num_bytes);
            *optlen = num_bytes;

            return 0;
        }
        case TCP_CONGESTION: {
            // the value of TCP_CA_NAME_MAX in linux
            const int CONG_NAME_MAX = 16;

            if (optval == NULL || optlen == NULL) {
                return -EINVAL;
            }

            char* dest_str = optval;
            const char* src_str = tcp_cong(tcp)->hooks->tcp_cong_name_str();

            if (src_str == NULL) {
                panic("Shadow's congestion type has no name!");
            }

            // the len value returned by linux seems to be independent from the actual string length
            *optlen = MIN(*optlen, CONG_NAME_MAX);

            if (*optlen > 0) {
                strncpy(dest_str, src_str, *optlen);
            }

            return 0;
        }
        default: {
            warning("getsockopt at level SOL_TCP called with unsupported option %i", optname);
            return -ENOPROTOOPT;
        }
    }
}

static int _syscallhandler_getSocketOptHelper(SysCallHandler* sys, LegacySocket* sock, int optname,
                                              void* optval, socklen_t* optlen) {
    switch (optname) {
        case SO_SNDBUF: {
            int sndbuf_size = legacysocket_getOutputBufferSize(sock);
            int num_bytes = MIN(*optlen, sizeof(sndbuf_size));
            memcpy(optval, &sndbuf_size, num_bytes);
            *optlen = num_bytes;
            return 0;
        }
        case SO_RCVBUF: {
            int rcvbuf_size = legacysocket_getInputBufferSize(sock);
            int num_bytes = MIN(*optlen, sizeof(rcvbuf_size));
            memcpy(optval, &rcvbuf_size, num_bytes);
            *optlen = num_bytes;
            return 0;
        }
        case SO_ERROR: {
            int error = 0;
            if (legacyfile_getType((LegacyFile*)sock) == DT_TCPSOCKET) {
                /* Return error for failed connect() attempts. */
                int connerr = tcp_getConnectionError((TCP*)sock);
                if (connerr == -ECONNRESET || connerr == -ECONNREFUSED) {
                    error = -connerr; // result is a positive errcode
                }
            }
            int num_bytes = MIN(*optlen, sizeof(error));
            memcpy(optval, &error, num_bytes);
            *optlen = num_bytes;
            return 0;
        }
        case SO_TYPE: {
            int sock_type = -1;

            switch (legacysocket_getProtocol(sock)) {
                case PMOCK: {
                    panic("Mock packet should not appear outside of tests");
                }
                case PNONE: {
                    panic("Socket has no protocol");
                }
                case PLOCAL: {
                    /* what is a PLOCAL socket? shadow doesn't seem to use it anywhere */
                    panic("Socket is a PLOCAL socket");
                }
                case PTCP: {
                    sock_type = SOCK_STREAM;
                    break;
                }
                case PUDP: {
                    sock_type = SOCK_DGRAM;
                    break;
                }
            }

            utility_alwaysAssert(sock_type >= 0);

            int num_bytes = MIN(*optlen, sizeof(sock_type));
            memcpy(optval, &sock_type, num_bytes);
            *optlen = num_bytes;
            return 0;
        }
        default: {
            warning("getsockopt at level SOL_SOCKET called with unsupported "
                    "option %i",
                    optname);
            return -ENOPROTOOPT;
        }
    }
}

static int _syscallhandler_setTCPOptHelper(SysCallHandler* sys, TCP* tcp, int optname,
                                           PluginPtr optvalPtr, socklen_t optlen) {
    switch (optname) {
        case TCP_NODELAY: {
            /* Shadow doesn't support nagle's algorithm, so shadow always behaves as if TCP_NODELAY
             * is enabled. Some programs will fail if setsockopt(fd, SOL_TCP, TCP_NODELAY, &1,
             * sizeof(int)) returns an error, so we treat this as a no-op for compatibility.
             */
            if (optlen < sizeof(int)) {
                return -EINVAL;
            }

            int enable = 0;
            int errcode = process_readPtr(sys->process, &enable, optvalPtr, sizeof(int));
            if (errcode != 0) {
                return errcode;
            }

            if (enable) {
                // wants to enable TCP_NODELAY
                debug("Ignoring TCP_NODELAY");
            } else {
                // wants to disable TCP_NODELAY
                warning("Cannot disable TCP_NODELAY since shadow does not implement "
                        "Nagle's algorithm.");
                return -ENOPROTOOPT;
            }

            return 0;
        }
        case TCP_CONGESTION: {
            // the value of TCP_CA_NAME_MAX in linux
            const int CONG_NAME_MAX = 16;

            char name[CONG_NAME_MAX];
            optlen = MIN(optlen, CONG_NAME_MAX);

            int errcode = process_readPtr(sys->process, name, optvalPtr, optlen);
            if (errcode != 0) {
                return errcode;
            }

            if (optlen < strlen(TCP_CONG_RENO_NAME) ||
                strncmp(name, TCP_CONG_RENO_NAME, optlen) != 0) {
                warning("Shadow sockets only support '%s' for TCP_CONGESTION", TCP_CONG_RENO_NAME);
                return -ENOENT;
            }

            // shadow doesn't support other congestion types, so do nothing
            const char* current_name = tcp_cong(tcp)->hooks->tcp_cong_name_str();
            utility_debugAssert(current_name != NULL &&
                                strcmp(current_name, TCP_CONG_RENO_NAME) == 0);
            return 0;
        }
        default: {
            warning("setsockopt on level SOL_TCP called with unsupported option %i", optname);
            return -ENOPROTOOPT;
        }
    }

    return 0;
}

static int _syscallhandler_setSocketOptHelper(SysCallHandler* sys, LegacySocket* sock, int optname,
                                              PluginPtr optvalPtr, socklen_t optlen) {
    if (optlen < sizeof(int)) {
        return -EINVAL;
    }

    switch (optname) {
        case SO_SNDBUF: {
            const unsigned int* val = process_getReadablePtr(sys->process, optvalPtr, sizeof(int));
            if (!val) {
                return -EFAULT;
            }
            size_t newsize = (size_t)(*val) * 2; // Linux kernel doubles this value upon setting

            // Linux also has limits SOCK_MIN_SNDBUF (slightly greater than 4096) and the sysctl max
            // limit. We choose a reasonable lower limit for Shadow. The minimum limit in man 7
            // socket is incorrect.
            newsize = MAX(newsize, 4096);

            // This upper limit was added as an arbitrarily high number so that we don't change
            // Shadow's behaviour, but also prevents an application from setting this to something
            // unnecessarily large like INT_MAX.
            newsize = MIN(newsize, 268435456); // 2^28 = 256 MiB

            legacysocket_setOutputBufferSize(sock, newsize);
            if (legacyfile_getType((LegacyFile*)sock) == DT_TCPSOCKET) {
                tcp_disableSendBufferAutotuning((TCP*)sock);
            }
            return 0;
        }
        case SO_RCVBUF: {
            const unsigned int* val = process_getReadablePtr(sys->process, optvalPtr, sizeof(int));
            size_t newsize = (size_t)(*val) * 2; // Linux kernel doubles this value upon setting

            // Linux also has limits SOCK_MIN_RCVBUF (slightly greater than 2048) and the sysctl max
            // limit. We choose a reasonable lower limit for Shadow. The minimum limit in man 7
            // socket is incorrect.
            newsize = MAX(newsize, 2048);

            // This upper limit was added as an arbitrarily high number so that we don't change
            // Shadow's behaviour, but also prevents an application from setting this to something
            // unnecessarily large like INT_MAX.
            newsize = MIN(newsize, 268435456); // 2^28 = 256 MiB

            legacysocket_setInputBufferSize(sock, newsize);
            if (legacyfile_getType((LegacyFile*)sock) == DT_TCPSOCKET) {
                tcp_disableReceiveBufferAutotuning((TCP*)sock);
            }
            return 0;
        }
        case SO_REUSEADDR: {
            // TODO implement this, tor and tgen use it
            trace("setsockopt SO_REUSEADDR not yet implemented");
            return 0;
        }
#ifdef SO_REUSEPORT
        case SO_REUSEPORT: {
            // TODO implement this, tgen uses it
            trace("setsockopt SO_REUSEPORT not yet implemented");
            return 0;
        }
#endif
        case SO_KEEPALIVE: {
            // TODO implement this, libevent uses it in
            // evconnlistener_new_bind()
            trace("setsockopt SO_KEEPALIVE not yet implemented");
            return 0;
        }
        case SO_BROADCAST: {
            // TODO implement this, pkg.go.dev/net uses it
            trace("setsockopt SO_BROADCAST not yet implemented");
            return 0;
        }
        default: {
            warning("setsockopt on level SOL_SOCKET called with unsupported "
                    "option %i",
                    optname);
            return -ENOPROTOOPT;
        }
    }

    return 0;
}

///////////////////////////////////////////////////////////
// Protected helpers
///////////////////////////////////////////////////////////

SysCallReturn _syscallhandler_recvfromHelper(SysCallHandler* sys, int sockfd,
                                             PluginPtr bufPtr, size_t bufSize,
                                             int flags, PluginPtr srcAddrPtr,
                                             PluginPtr addrlenPtr) {
    trace("trying to recv %zu bytes on socket %i", bufSize, sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);

    LegacyFile* desc = (LegacyFile*)socket_desc;
    if (errcode < 0 && _syscallhandler_readableWhenClosed(sys, desc)) {
        errcode = 0;
    }

    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    if (flags & ~MSG_DONTWAIT) {
        warning("Unsupported recv flag(s): %d", flags);
    }

    ssize_t retval = 0;

    if (legacyfile_getType(desc) == DT_TCPSOCKET) {
        int errcode = tcp_getConnectionError((TCP*)socket_desc);

        if (errcode > 0) {
            /* connect() was not called yet. */
            return syscallreturn_makeDoneErrno(ENOTCONN);
        } else if (errcode == -EALREADY) {
            /* Connection in progress. */
            retval = -EWOULDBLOCK;
        }
    }

    struct sockaddr_in inet_addr = {.sin_family = AF_INET};

    if (retval == 0) {
        size_t sizeNeeded = bufSize;

        if (legacyfile_getType(desc) == DT_TCPSOCKET) {
            // we can only truncate the data if it is a TCP connection
            /* TODO: Dynamically compute size based on how much data is actually
             * available in the descriptor. */
            sizeNeeded = MIN(sizeNeeded, SYSCALL_IO_BUFSIZE);
        } else if (legacyfile_getType(desc) == DT_UDPSOCKET) {
            // allow it to be 1 byte longer than the max datagram size
            sizeNeeded = MIN(sizeNeeded, CONFIG_DATAGRAM_MAX_SIZE + 1);
        }

        retval = transport_receiveUserData((Transport*)socket_desc, sys->thread, bufPtr, sizeNeeded,
                                           &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);

        trace("recv returned %zd", retval);
    }

    bool nonblocking_mode = legacyfile_getFlags(desc) & O_NONBLOCK || flags & MSG_DONTWAIT;
    if (retval == -EWOULDBLOCK && !nonblocking_mode) {
        trace("recv would block on socket %i", sockfd);
        /* We need to block until the descriptor is ready to read. */
        Trigger trigger =
            (Trigger){.type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_FILE_READABLE};
        return syscallreturn_makeBlocked(
            syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
    }

    /* check if they wanted to know where we got the data from */
    if (retval > 0 && srcAddrPtr.val) {
        trace("address info is requested in recv on socket %i", sockfd);

        /* only write an address for UDP sockets */
        if (legacyfile_getType(desc) == DT_UDPSOCKET) {
            errcode = _syscallhandler_getnameHelper(
                sys, (struct sockaddr*)&inet_addr, sizeof(inet_addr), srcAddrPtr, addrlenPtr);
            if (errcode) {
                return syscallreturn_makeDoneErrno(-errcode);
            }
        } else {
            /* set the address length as 0 */
            socklen_t addrlen = 0;
            if (process_writePtr(sys->process, addrlenPtr, &addrlen, sizeof(addrlen)) != 0) {
                return syscallreturn_makeDoneErrno(EFAULT);
            }
        }
    }

    return syscallreturn_makeDoneI64(retval);
}

SysCallReturn _syscallhandler_sendtoHelper(SysCallHandler* sys, int sockfd,
                                           PluginPtr bufPtr, size_t bufSize,
                                           int flags, PluginPtr destAddrPtr,
                                           socklen_t addrlen) {
    trace("trying to send %zu bytes on socket %i", bufSize, sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Need non-NULL buffer. */
    /* FIXME: should push this check to the point the data is actually read,
     * to correctly handle non-NULL pointers that aren't accessible.
     * This is currently in the Payload code; need to bubble up errors from there.
     */
    if (!bufPtr.val) {
        debug("Can't send from NULL buffer on socket %i", sockfd);
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    /* TODO: when we support AF_UNIX this could be sockaddr_un */
    size_t inet_len = sizeof(struct sockaddr_in);
    if (destAddrPtr.val && addrlen < inet_len) {
        debug("Address length %ld is too small on socket %i", (long int)addrlen, sockfd);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    if (flags & ~MSG_DONTWAIT) {
        warning("Unsupported send flag(s): %d", flags);
    }

    /* Get the address info if they specified one. */
    in_addr_t dest_ip = 0;
    in_port_t dest_port = 0;

    if (destAddrPtr.val) {
        const struct sockaddr* dest_addr =
            process_getReadablePtr(sys->process, destAddrPtr, addrlen);
        utility_debugAssert(dest_addr);

        /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
        if (dest_addr->sa_family != AF_INET) {
            warning(
                "We only support address family AF_INET on socket %i", sockfd);
            return syscallreturn_makeDoneErrno(EAFNOSUPPORT);
        }

        dest_ip = ((struct sockaddr_in*)dest_addr)->sin_addr.s_addr;
        dest_port = ((struct sockaddr_in*)dest_addr)->sin_port;
    }

    LegacyFile* desc = (LegacyFile*)socket_desc;
    errcode = 0;

    if (legacyfile_getType(desc) == DT_UDPSOCKET) {
        /* make sure that we have somewhere to send it */
        if (dest_ip == 0 || dest_port == 0) {
            /* its ok if they setup a default destination with connect() */
            legacysocket_getPeerName(socket_desc, &dest_ip, &dest_port);
            if (dest_ip == 0 || dest_port == 0) {
                /* we have nowhere to send it */
                return syscallreturn_makeDoneErrno(EDESTADDRREQ);
            }
        }

        /* if this socket is not bound, do an implicit bind to a random port */
        if (!legacysocket_isBound(socket_desc)) {
            ProtocolType ptype = legacysocket_getProtocol(socket_desc);

            /* We don't bind to peer ip/port since that might change later. */
            in_addr_t bindAddr =
                (dest_ip == htonl(INADDR_LOOPBACK))
                    ? htonl(INADDR_LOOPBACK)
                    : address_toNetworkIP(host_getDefaultAddress(_syscallhandler_getHost(sys)));
            in_port_t bindPort =
                host_getRandomFreePort(_syscallhandler_getHost(sys), ptype, bindAddr, 0, 0);

            if (!bindPort) {
                return syscallreturn_makeDoneErrno(EADDRNOTAVAIL);
            }

            /* connect up socket layer */
            legacysocket_setPeerName(socket_desc, 0, 0);
            legacysocket_setSocketName(socket_desc, bindAddr, bindPort);

            /* set netiface->socket associations */
            CompatSocket compat_socket = compatsocket_fromLegacySocket(socket_desc);
            host_associateInterface(_syscallhandler_getHost(sys), &compat_socket, bindAddr);
        }
    } else if (legacyfile_getType(desc) == DT_TCPSOCKET) {
        errcode = tcp_getConnectionError((TCP*)socket_desc);

        trace("connection error state is currently %i", errcode);

        if (errcode > 0) {
            /* connect() was not called yet.
             * TODO: Can they can piggy back a connect() on sendto() if they
             * provide an address for the connection? */
            return syscallreturn_makeDoneErrno(EPIPE);
        } else if (errcode == 0) {
            /* They connected, but never read the success code with a second
             * call to connect(). That's OK, proceed to send as usual. */
        } else if (errcode == -EISCONN) {
            /* They are connected, and we can send now. */
            errcode = 0;
        } else if (errcode == -EALREADY) {
            /* Connection in progress.
             * TODO: should we wait, or just return -EALREADY? */
            errcode = -EWOULDBLOCK;
        }
    }

    gssize retval = (gssize)errcode;

    if (errcode == 0) {
        size_t sizeNeeded = bufSize;

        if (legacyfile_getType(desc) == DT_TCPSOCKET) {
            // we can only truncate the data if it is a TCP connection
            /* TODO: Dynamically compute size based on how much data is actually
             * available in the descriptor. */
            sizeNeeded = MIN(sizeNeeded, SYSCALL_IO_BUFSIZE);
        } else if (legacyfile_getType(desc) == DT_UDPSOCKET) {
            // allow it to be 1 byte longer than the max so that we can receive EMSGSIZE
            sizeNeeded = MIN(sizeNeeded, CONFIG_DATAGRAM_MAX_SIZE + 1);
        }

        retval = transport_sendUserData(
            (Transport*)socket_desc, sys->thread, bufPtr, sizeNeeded, dest_ip, dest_port);

        trace("send returned %zd", retval);
    }

    bool nonblocking_mode = legacyfile_getFlags(desc) & O_NONBLOCK || flags & MSG_DONTWAIT;
    if (retval == -EWOULDBLOCK && !nonblocking_mode) {
        if (bufSize > 0) {
            /* We need to block until the descriptor is ready to write. */
            Trigger trigger = (Trigger){
                .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_FILE_WRITABLE};
            return syscallreturn_makeBlocked(
                syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
        } else {
            /* We attempted to write 0 bytes, so no need to block or return EWOULDBLOCK. */
            retval = 0;
        }
    }

    return syscallreturn_makeDoneI64(retval);
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_accept(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_acceptHelper(sys, (int)args->args[0].as_i64,
                                        args->args[1].as_ptr,
                                        args->args[2].as_ptr, 0);
}

SysCallReturn syscallhandler_accept4(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    return _syscallhandler_acceptHelper(
        sys, (int)args->args[0].as_i64, args->args[1].as_ptr,
        args->args[2].as_ptr, args->args[3].as_i64);
}

SysCallReturn syscallhandler_bind(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    int sockfd = (int)args->args[0].as_i64;
    PluginPtr addrPtr = args->args[1].as_ptr; // const struct sockaddr*
    socklen_t addrlen = (socklen_t)args->args[2].as_u64;

    trace("trying to bind on socket %i", sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    /* It's an error if it is already bound. */
    if (legacysocket_isBound(socket_desc)) {
        debug("socket descriptor %i is already bound to an address", sockfd);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    const struct sockaddr* addr = process_getReadablePtr(sys->process, addrPtr, addrlen);
    if (addr == NULL) {
        debug("Invalid bind address pointer");
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if (addrlen < inet_len) {
        debug("supplied address is not large enough for a inet address");
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if (addr->sa_family != AF_INET) {
        warning("binding to address family %i, but we only support AF_INET",
                (int)addr->sa_family);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get the requested address and port. */
    struct sockaddr_in* inet_addr = (struct sockaddr_in*)addr;
    in_addr_t bindAddr = inet_addr->sin_addr.s_addr;
    in_port_t bindPort = inet_addr->sin_port;

    errcode =
        _syscallhandler_bindHelper(sys, socket_desc, bindAddr, bindPort, 0, 0);
    return syscallreturn_makeDoneI64(errcode);
}

SysCallReturn syscallhandler_connect(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    PluginPtr addrPtr = args->args[1].as_ptr; // const struct sockaddr*
    socklen_t addrlen = args->args[2].as_u64;

    trace("trying to connect on socket %i", sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if (addrlen < inet_len) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    const struct sockaddr* addr = process_getReadablePtr(sys->process, addrPtr, addrlen);
    if (!addr) {
        debug("Couldn't read addr %p", (void*)addrPtr.val);
        return syscallreturn_makeDoneErrno(EFAULT);
    }


    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if (addr->sa_family != AF_INET && addr->sa_family != AF_UNSPEC) {
        warning("connecting to address family %i, but we only support AF_INET",
                (int)addr->sa_family);
        return syscallreturn_makeDoneErrno(EAFNOSUPPORT);
    } else if (!legacysocket_isFamilySupported(socket_desc, addr->sa_family)) {
        return syscallreturn_makeDoneErrno(EAFNOSUPPORT);
    }

    /* TODO: update for AF_UNIX */
    struct sockaddr_in* inet_addr = (struct sockaddr_in*)addr;

    sa_family_t family = inet_addr->sin_family;
    in_addr_t peerAddr = inet_addr->sin_addr.s_addr;
    in_port_t peerPort = inet_addr->sin_port;
    in_addr_t loopbackAddr = htonl(INADDR_LOOPBACK);

    if (peerAddr == htonl(INADDR_ANY)) {
        peerAddr = loopbackAddr;
    }

    /* make sure we will be able to route this later */
    if (peerAddr != loopbackAddr) {
        const Address* myAddress = host_getDefaultAddress(_syscallhandler_getHost(sys));
        const Address* peerAddress = worker_resolveIPToAddress(peerAddr);
        in_addr_t myAddr = htonl(address_toHostIP(myAddress));
        if (!peerAddress || !worker_isRoutable(myAddr, peerAddr)) {
            /* can't route it - there is no node with this address */
            gchar* peerAddressString = address_ipToNewString(peerAddr);
            warning("attempting to connect to address '%s:%u' for which no "
                    "host exists",
                    peerAddressString, ntohs(peerPort));
            g_free(peerAddressString);
            return syscallreturn_makeDoneErrno(ECONNREFUSED);
        }
    }

    if (!legacysocket_isBound(socket_desc)) {
        /* do an implicit bind to a random ephemeral port.
         * use default interface unless the remote peer is on loopback */
        in_addr_t bindAddr = (loopbackAddr == peerAddr)
                                 ? loopbackAddr
                                 : host_getDefaultIP(_syscallhandler_getHost(sys));
        errcode = _syscallhandler_bindHelper(
            sys, socket_desc, bindAddr, 0, peerAddr, peerPort);
        if (errcode < 0) {
            return syscallreturn_makeDoneErrno(-errcode);
        }
    } else {
        legacysocket_setPeerName(socket_desc, peerAddr, peerPort);
    }

    /* Now we are ready to connect. */
    errcode = legacysocket_connectToPeer(
        socket_desc, _syscallhandler_getHost(sys), peerAddr, peerPort, family);

    LegacyFile* desc = (LegacyFile*)socket_desc;
    if (legacyfile_getType(desc) == DT_TCPSOCKET && !(legacyfile_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking connect call. */
        if (errcode == -EINPROGRESS) {
            /* This is the first time we ever called connect, and so we
             * need to wait for the 3-way handshake to complete.
             * We will wait indefinitely for a success or failure. */
            Trigger trigger = (Trigger){.type = TRIGGER_DESCRIPTOR,
                                        .object = desc,
                                        .status = STATUS_FILE_ACTIVE | STATUS_FILE_WRITABLE};
            return syscallreturn_makeBlocked(
                syscallcondition_new(trigger), legacyfile_supportsSaRestart(desc));
        } else if (_syscallhandler_wasBlocked(sys) && errcode == -EISCONN) {
            /* It was EINPROGRESS, but is now a successful blocking connect. */
            errcode = 0;
        }
    }

    /* Make sure we return valid error codes for connect. */
    if (errcode == -ECONNRESET || errcode == -ENOTCONN) {
        errcode = -EISCONN;
    }
    /* -EALREADY is well defined in man page, but Linux returns -EINPROGRESS. */
    else if (errcode == -EALREADY) {
        errcode = -EINPROGRESS;
    }

    /* Return 0, -EINPROGRESS, etc. now. */
    return syscallreturn_makeDoneI64(errcode);
}

SysCallReturn syscallhandler_getpeername(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;

    trace("trying to get peer name on socket %i", sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    // TODO I'm not sure if we should be able to get the peer name on UDP
    // sockets. If you call connect on it, then getpeername should probably
    // return the peer you associated in the most recent connect call.
    // If we can validate that, we can delete this comment.
    //    /* Only a TCP socket can be connected to a peer.
    //     * TODO: Needs to be updated when we support AF_UNIX. */
    //    LegacyFileType type = legacyfile_getType((LegacyFile*)socket_desc);
    //    if(type != DT_TCPSOCKET) {
    //        info("descriptor %i is not a TCP socket", sockfd);
    //        return syscallreturn_makeErrno(ENOTCONN);
    //    }

    /* Get the name of the connected peer.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr saddr = {0};
    size_t slen = 0;

    struct sockaddr_in* inet_addr = (struct sockaddr_in*)&saddr;
    inet_addr->sin_family = AF_INET;

    gboolean hasName =
        legacysocket_getPeerName(socket_desc, &inet_addr->sin_addr.s_addr, &inet_addr->sin_port);
    if (!hasName) {
        debug("Socket %i has no peer name.", sockfd);
        return syscallreturn_makeDoneErrno(ENOTCONN);
    }

    slen = sizeof(*inet_addr);

    /* Use helper to write out the result. */
    return syscallreturn_makeDoneI64(_syscallhandler_getnameHelper(
        sys, &saddr, slen, args->args[1].as_ptr, args->args[2].as_ptr));
}

SysCallReturn syscallhandler_getsockname(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;

    trace("trying to get sock name on socket %i", sockfd);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    /* Get the name of the socket.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr saddr = {0};
    size_t slen = 0;

    struct sockaddr_in* inet_addr = (struct sockaddr_in*)&saddr;
    inet_addr->sin_family = AF_INET;

    gboolean hasName =
        legacysocket_getSocketName(socket_desc, &inet_addr->sin_addr.s_addr, &inet_addr->sin_port);
    /* If !hasName, leave sin_addr and sin_port at their default 0 values. */

    /* If we are bound to INADDR_ANY, we should instead return the address used
     * to communicate with the connected peer (if we have one). */
    if (inet_addr->sin_addr.s_addr == htonl(INADDR_ANY)) {
        in_addr_t peerIP = 0;
        if (legacysocket_getPeerName(socket_desc, &peerIP, NULL) &&
            peerIP != htonl(INADDR_LOOPBACK)) {
            inet_addr->sin_addr.s_addr = host_getDefaultIP(_syscallhandler_getHost(sys));
        }
    }

    slen = sizeof(*inet_addr);

    /* Use helper to write out the result. */
    return syscallreturn_makeDoneI64(_syscallhandler_getnameHelper(
        sys, &saddr, slen, args->args[1].as_ptr, args->args[2].as_ptr));
}

SysCallReturn syscallhandler_getsockopt(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int level = args->args[1].as_i64;
    int optname = args->args[2].as_i64;
    PluginPtr optvalPtr = args->args[3].as_ptr; // void*
    PluginPtr optlenPtr = args->args[4].as_ptr; // socklen_t*

    trace("trying to getsockopt on socket %i at level %i for opt %i", sockfd,
          level, optname);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    socklen_t optlen;
    if (process_readPtr(sys->process, &optlen, optlenPtr, sizeof(optlen)) != 0) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    /* Return early if there are no bytes to store data. */
    if (optlen == 0) {
        return syscallreturn_makeDoneI64(0);
    }

    /* The optval pointer must be non-null since optlen is non-zero. */

    MemoryManager* mm = process_getMemoryManager(sys->process);
    ProcessMemoryRefMut_u8* optvalref = memorymanager_getWritablePtr(mm, optvalPtr, optlen);
    if (!optvalref) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }
    void* optval = memorymanagermut_ptr(optvalref);
    if (!optval) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    errcode = 0;
    switch (level) {
        case SOL_TCP: {
            if (legacyfile_getType((LegacyFile*)socket_desc) != DT_TCPSOCKET) {
                errcode = -EOPNOTSUPP;
                break;
            }

            errcode =
                _syscallhandler_getTCPOptHelper(sys, (TCP*)socket_desc, optname, optval, &optlen);
            break;
        }
        case SOL_SOCKET: {
            errcode =
                _syscallhandler_getSocketOptHelper(sys, socket_desc, optname, optval, &optlen);
            break;
        }
        default:
            warning("getsockopt called with unsupported level %i with opt %i", level, optname);
            errcode = -ENOPROTOOPT;
            break;
    }

    if (errcode) {
        memorymanager_freeMutRefWithoutFlush(optvalref);
        return syscallreturn_makeDoneErrno(-errcode);
    }

    errcode = memorymanager_freeMutRefWithFlush(optvalref);
    if (errcode) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    errcode = process_writePtr(sys->process, optlenPtr, &optlen, sizeof(optlen));
    if (errcode) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(0);
}

SysCallReturn syscallhandler_listen(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int backlog = args->args[1].as_i64;

    trace("trying to listen on socket %i", sockfd);

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errcode =
        _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(tcp_desc);

    /* only listen on the socket if it is not used for other functions */
    if (!tcp_isListeningAllowed(tcp_desc)) {
        debug("Cannot listen on previously used socket %i", sockfd);
        return syscallreturn_makeDoneErrno(EOPNOTSUPP);
    }

    /* if we are already listening, just update the backlog and return 0. */
    if (tcp_isValidListener(tcp_desc)) {
        trace("Socket %i already set up as a listener; updating backlog", sockfd);
        tcp_updateServerBacklog(tcp_desc, backlog);
        return syscallreturn_makeDoneU64(0);
    }

    /* We are allowed to listen but not already listening, start now. */
    if (!legacysocket_isBound((LegacySocket*)tcp_desc)) {
        /* Implicit bind: bind to all interfaces at an ephemeral port. */
        trace("Implicitly binding listener socket %i", sockfd);
        errcode =
            _syscallhandler_bindHelper(sys, (LegacySocket*)tcp_desc, htonl(INADDR_ANY), 0, 0, 0);
        if (errcode < 0) {
            return syscallreturn_makeDoneErrno(-errcode);
        }
    }

    tcp_enterServerMode(tcp_desc, _syscallhandler_getHost(sys), sys->process, backlog);
    return syscallreturn_makeDoneU64(0);
}

SysCallReturn syscallhandler_recvfrom(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    return _syscallhandler_recvfromHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_i64, args->args[4].as_ptr, args->args[5].as_ptr);
}

SysCallReturn syscallhandler_sendto(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    return _syscallhandler_sendtoHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_u64,
        args->args[3].as_i64, args->args[4].as_ptr, args->args[5].as_u64);
}

SysCallReturn syscallhandler_setsockopt(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int level = args->args[1].as_i64;
    int optname = args->args[2].as_i64;
    PluginPtr optvalPtr = args->args[3].as_ptr; // const void*
    socklen_t optlen = args->args[4].as_u64;

    trace("trying to setsockopt on socket %i at level %i for opt %i", sockfd,
          level, optname);

    /* Get and validate the socket. */
    LegacySocket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    utility_debugAssert(socket_desc);

    /* Return early if there is no data. */
    if (optlen == 0) {
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    errcode = 0;
    switch (level) {
        case SOL_TCP: {
            if (legacyfile_getType((LegacyFile*)socket_desc) != DT_TCPSOCKET) {
                errcode = -ENOPROTOOPT;
                break;
            }

            errcode =
                _syscallhandler_setTCPOptHelper(sys, (TCP*)socket_desc, optname, optvalPtr, optlen);
            break;
        }
        case SOL_SOCKET: {
            errcode = _syscallhandler_setSocketOptHelper(
                sys, socket_desc, optname, optvalPtr, optlen);
            break;
        }
        default:
            warning("setsockopt called with unsupported level %i with opt %i", level, optname);
            errcode = -ENOPROTOOPT;
    }

    return syscallreturn_makeDoneI64(errcode);
}

SysCallReturn syscallhandler_shutdown(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int how = args->args[1].as_i64;

    trace("trying to shutdown on socket %i with how %i", sockfd, how);

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        debug("invalid how %i", how);
        return syscallreturn_makeDoneErrno(EINVAL);
    }

    /* Get and validate the socket. */
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, NULL);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    TCP* tcp_desc = NULL;
    errcode = _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    if (errcode >= 0) {
        return syscallreturn_makeDoneI64(tcp_shutdown(tcp_desc, _syscallhandler_getHost(sys), how));
    }

    UDP* udp_desc = NULL;
    errcode = _syscallhandler_validateUDPSocketHelper(sys, sockfd, &udp_desc);
    if (errcode >= 0) {
        return syscallreturn_makeDoneI64(udp_shutdown(udp_desc, how));
    }

    warning("socket %d is neither a TCP nor UDP socket", sockfd);
    return syscallreturn_makeDoneErrno(ENOTCONN);
}

SysCallReturn syscallhandler_socket(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int domain = args->args[0].as_i64;
    int type = args->args[1].as_i64;
    int protocol = args->args[2].as_i64;

    trace("trying to create new socket");

    /* Remove the two possible flags to get the type. */
    int type_no_flags = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    /* TODO: add support for AF_UNIX? */
    /* The below are warnings so the Shadow user knows that we don't support
     * everything that Linux supports. */
    if (domain != AF_INET) {
        warning("unsupported socket domain \"%i\", we only support AF_INET",
                domain);
        return syscallreturn_makeDoneErrno(EAFNOSUPPORT);
    } else if (type_no_flags != SOCK_STREAM && type_no_flags != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM "
                "and SOCK_DGRAM",
                type_no_flags);
        return syscallreturn_makeDoneErrno(ESOCKTNOSUPPORT);
    } else if (type_no_flags == SOCK_STREAM && protocol != 0 && protocol != IPPROTO_TCP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_TCP on sockets of type SOCK_STREAM", protocol);
        return syscallreturn_makeDoneErrno(EPROTONOSUPPORT);
    } else if (type_no_flags == SOCK_DGRAM && protocol != 0 && protocol != IPPROTO_UDP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_UDP on sockets of type SOCK_DGRAM", protocol);
        return syscallreturn_makeDoneErrno(EPROTONOSUPPORT);
    }

    /* We are all set to create the socket. */
    guint64 recvBufSize = host_getConfiguredRecvBufSize(_syscallhandler_getHost(sys));
    guint64 sendBufSize = host_getConfiguredSendBufSize(_syscallhandler_getHost(sys));

    LegacySocket* sock_desc = NULL;
    if (type_no_flags == SOCK_STREAM) {
        sock_desc = (LegacySocket*)tcp_new(_syscallhandler_getHost(sys), recvBufSize, sendBufSize);
    } else {
        sock_desc = (LegacySocket*)udp_new(_syscallhandler_getHost(sys), recvBufSize, sendBufSize);
    }

    int descFlags = 0;
    if (type & SOCK_CLOEXEC) {
        descFlags |= O_CLOEXEC;
    }

    /* Now make sure it will be valid when we operate on it. */
    Descriptor* desc = descriptor_fromLegacyFile((LegacyFile*)sock_desc, descFlags);
    int sockfd = process_registerDescriptor(sys->process, desc);

    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, NULL);
    if (errcode != 0) {
        utility_panic("Unable to find socket %i that we just created.", sockfd);
    }
    utility_debugAssert(errcode == 0);

    /* Set any options that were given. */
    if (type & SOCK_NONBLOCK) {
        legacyfile_addFlags(&sock_desc->super.super, O_NONBLOCK);
    }

    trace("socket() returning fd %i", sockfd);

    return syscallreturn_makeDoneI64(sockfd);
}
