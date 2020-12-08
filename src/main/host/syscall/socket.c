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

#include "main/core/worker.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/udp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Private Helpers
///////////////////////////////////////////////////////////

/* It's valid to read data from a socket even if close() was already called,
 * as long as the EOF has not yet been read (i.e., there is still data that
 * must be read from the socket). This function checks if the descriptor is
 * in this corner case and we should be allowed to read from it. */
static bool _syscallhandler_readableWhenClosed(SysCallHandler* sys,
                                               Descriptor* desc) {
    if (desc && descriptor_getType(desc) == DT_TCPSOCKET &&
        (descriptor_getStatus(desc) & STATUS_DESCRIPTOR_CLOSED)) {
        /* Connection error will be -ENOTCONN when reading is done. */
        if (tcp_getConnectionError((TCP*)desc) == -EISCONN) {
            return true;
        }
    }
    return false;
}

static int _syscallhandler_validateSocketHelper(SysCallHandler* sys, int sockfd,
                                                Socket** sock_desc_out) {
    /* Check that fd is within bounds. */
    if (sockfd < 0) {
        info("descriptor %i out of bounds", sockfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* desc = process_getRegisteredDescriptor(sys->process, sockfd);
    if (desc && sock_desc_out) {
        *sock_desc_out = (Socket*)desc;
    }

    int errcode = _syscallhandler_validateDescriptor(desc, DT_NONE);
    if (errcode) {
        info("descriptor %i is invalid", sockfd);
        return errcode;
    }

    DescriptorType type = descriptor_getType(desc);
    if (type != DT_TCPSOCKET && type != DT_UDPSOCKET && type != DT_UNIXSOCKET) {
        info("descriptor %i with type %i is not a socket", sockfd, (int)type);
        return -ENOTSOCK;
    }

    /* Now we know we have a valid socket. */
    return 0;
}

static int _syscallhandler_validateTCPSocketHelper(SysCallHandler* sys,
                                                   int sockfd,
                                                   TCP** tcp_desc_out) {
    Socket* sock_desc = NULL;
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, &sock_desc);

    if (sock_desc && tcp_desc_out) {
        *tcp_desc_out = (TCP*)sock_desc;
    }
    if (errcode) {
        return errcode;
    }

    DescriptorType type = descriptor_getType((Descriptor*)sock_desc);
    if (type != DT_TCPSOCKET) {
        info("descriptor %i is not a TCP socket", sockfd);
        return -EOPNOTSUPP;
    }

    /* Now we know we have a valid TCP socket. */
    return 0;
}

static int _syscallhandler_validateUDPSocketHelper(SysCallHandler* sys,
                                                   int sockfd,
                                                   UDP** udp_desc_out) {
    Socket* sock_desc = NULL;
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, &sock_desc);

    if (sock_desc && udp_desc_out) {
        *udp_desc_out = (UDP*)sock_desc;
    }
    if (errcode) {
        return errcode;
    }

    DescriptorType type = descriptor_getType((Descriptor*)sock_desc);
    if (type != DT_UDPSOCKET) {
        info("descriptor %i is not a UDP socket", sockfd);
        return -EOPNOTSUPP;
    }

    /* Now we know we have a valid UDP socket. */
    return 0;
}

static SysCallReturn
_syscallhandler_getnameHelper(SysCallHandler* sys,
                              struct sockaddr_in* inet_addr, PluginPtr addrPtr,
                              PluginPtr addrlenPtr) {
    if (!addrPtr.val || !addrlenPtr.val) {
        info("Cannot get name with NULL return address info.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    socklen_t* addrlen =
        process_getMutablePtr(sys->process, sys->thread, addrlenPtr, sizeof(*addrlen));

    /* The result is truncated if they didn't give us enough space. */
    size_t retSize = MIN(*addrlen, sizeof(*inet_addr));
    *addrlen = (socklen_t)sizeof(*inet_addr);

    if (retSize > 0) {
        /* Return the results */
        struct sockaddr* addr =
            process_getWriteablePtr(sys->process, sys->thread, addrPtr, retSize);
        memcpy(addr, inet_addr, retSize);
    }

    return (SysCallReturn){.state = SYSCALL_DONE};
}

static SysCallReturn _syscallhandler_acceptHelper(SysCallHandler* sys,
                                                  int sockfd, PluginPtr addrPtr,
                                                  PluginPtr addrlenPtr,
                                                  int flags) {
    debug("trying to accept on socket %i", sockfd);

    /* Check that non-valid flags are not given. */
    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
        info("invalid flags \"%i\", only SOCK_NONBLOCK and SOCK_CLOEXEC are "
             "allowed",
             flags);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errcode =
        _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);

    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(tcp_desc);

    /* We must be listening in order to accept. */
    if (!tcp_isValidListener(tcp_desc)) {
        info("socket %i is not listening", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Make sure they supplied addrlen if they requested an addr. */
    if (addrPtr.val && !addrlenPtr.val) {
        info("addrlen was NULL when addr was non-NULL");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* OK, now we can check if we have anything to accept. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    int accepted_fd = 0;
    errcode = tcp_acceptServerPeer(
        tcp_desc, &inet_addr.sin_addr.s_addr, &inet_addr.sin_port, &accepted_fd);

    Descriptor* desc = (Descriptor*)tcp_desc;
    if (errcode == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking accept, and we don't have a connection yet.
         * The socket becomes readable when we have a connection to accept.
         * This blocks indefinitely without a timeout. */
        debug("Listening socket %i waiting for acceptable connection.", sockfd);
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_READABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    } else if (errcode < 0) {
        debug("TCP error when accepting connection on socket %i", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* We accepted something! */
    utility_assert(accepted_fd > 0);
    TCP* accepted_tcp_desc = NULL;
    errcode = _syscallhandler_validateTCPSocketHelper(
        sys, accepted_fd, &accepted_tcp_desc);
    utility_assert(errcode == 0);

    debug("listening socket %i accepted fd %i", sockfd, accepted_fd);

    /* Set the flags on the accepted socket if requested. */
    if (flags & SOCK_NONBLOCK) {
        descriptor_addFlags((Descriptor*)accepted_tcp_desc, O_NONBLOCK);
    }
    if (flags & SOCK_CLOEXEC) {
        descriptor_addFlags((Descriptor*)accepted_tcp_desc, O_CLOEXEC);
    }

    /* check if they wanted to know where we got the data from */
    if (addrPtr.val) {
        _syscallhandler_getnameHelper(sys, &inet_addr, addrPtr, addrlenPtr);
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = accepted_fd};
}

static int _syscallhandler_bindHelper(SysCallHandler* sys, Socket* socket_desc,
                                      in_addr_t addr, in_port_t port,
                                      in_addr_t peerAddr, in_port_t peerPort) {
#ifdef DEBUG
    gchar* bindAddrStr = address_ipToNewString(addr);
    gchar* peerAddrStr = address_ipToNewString(peerAddr);
    debug("trying to bind to inet address %s:%u on socket %i with peer %s:%u",
          bindAddrStr, ntohs(port),
          descriptor_getHandle((Descriptor*)socket_desc), peerAddrStr,
          ntohs(peerPort));
    g_free(bindAddrStr);
    g_free(peerAddrStr);
#endif

    /* make sure we have an interface at that address */
    if (!host_doesInterfaceExist(sys->host, addr)) {
        info("no network interface exists for the provided bind address");
        return -EINVAL;
    }

    /* Each protocol type gets its own ephemeral port mapping. */
    ProtocolType ptype = socket_getProtocol(socket_desc);

    /* Get a free ephemeral port if they didn't specify one. */
    if (port == 0) {
        port =
            host_getRandomFreePort(sys->host, ptype, addr, peerAddr, peerPort);
        debug("binding to generated ephemeral port %u", ntohs(port));
    }

    /* Ephemeral port unavailable. */
    if (port == 0) {
        info("binding required an ephemeral port and none are available");
        return -EADDRINUSE;
    }

    /* Make sure the port is available at this address for this protocol. */
    if (!host_isInterfaceAvailable(
            sys->host, ptype, addr, port, peerAddr, peerPort)) {
        info("the provided address and port %u are not available", ntohs(port));
        return -EADDRINUSE;
    }

    /* bind port and set associations */
    host_associateInterface(
        sys->host, socket_desc, addr, port, peerAddr, peerPort);
    return 0;
}

static int _syscallhandler_getTCPOptHelper(SysCallHandler* sys, TCP* tcp,
                                           int optname, PluginPtr optvalPtr,
                                           PluginPtr optlenPtr) {
    switch (optname) {
        case TCP_INFO: {
            /* Get the len via clone, so we can write to optlenPtr too. */
            socklen_t* optlen =
                process_getMutablePtr(sys->process, sys->thread, optlenPtr, sizeof(*optlen));
            size_t sizeNeeded = sizeof(struct tcp_info);

            if (*optlen < sizeNeeded) {
                info("Unable to store tcp info in %zu bytes.", (size_t)*optlen);
                return -EINVAL;
            }

            /* Write the tcp info and its size. */
            *optlen = sizeNeeded;
            struct tcp_info* info =
                process_getWriteablePtr(sys->process, sys->thread, optvalPtr, sizeNeeded);
            tcp_getInfo(tcp, info);

            return 0;
        }
        default: {
            warning(
                "getsockopt at level SOL_TCP called with unsupported option %i",
                optname);
            return -ENOPROTOOPT;
        }
    }
}

static int _syscallhandler_getSocketOptHelper(SysCallHandler* sys, Socket* sock,
                                              int optname, PluginPtr optvalPtr,
                                              PluginPtr optlenPtr) {
    /* Get the len via clone, so we can write to optlenPtr too. */
    const socklen_t* optlen =
        process_getReadablePtr(sys->process, sys->thread, optlenPtr, sizeof(*optlen));

    /* All options we currently support are integers. When adding support for
     * more options, make sure to check length requirements. */
    if (*optlen < sizeof(int)) {
        info("Unable to store option in %zu bytes.", (size_t)*optlen);
        return -EINVAL;
    }

    int* optval = process_getWriteablePtr(sys->process, sys->thread, optvalPtr, sizeof(int));

    switch (optname) {
        case SO_SNDBUF: {
            *optval = socket_getOutputBufferSize(sock);
            return 0;
        }
        case SO_RCVBUF: {
            *optval = socket_getInputBufferSize(sock);
            return 0;
        }
        case SO_ERROR: {
            *optval = 0;
            if (descriptor_getType((Descriptor*)sock) == DT_TCPSOCKET) {
                /* Return error for failed connect() attempts. */
                int connerr = tcp_getConnectionError((TCP*)sock);
                if (connerr == -ECONNRESET || connerr == -ECONNREFUSED) {
                    *optval = -connerr; // result is a positive errcode
                }
            }
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

static int _syscallhandler_setSocketOptHelper(SysCallHandler* sys, Socket* sock,
                                              int optname, PluginPtr optvalPtr,
                                              socklen_t optlen) {
    if (optlen < sizeof(int)) {
        return -EINVAL;
    }

    switch (optname) {
        case SO_SNDBUF: {
            const unsigned int* val =
                process_getReadablePtr(sys->process, sys->thread, optvalPtr, sizeof(int));
            size_t newsize =
                (*val) * 2; // Linux kernel doubles this value upon setting
            socket_setOutputBufferSize(sock, newsize);
            if (descriptor_getType((Descriptor*)sock) == DT_TCPSOCKET) {
                tcp_disableSendBufferAutotuning((TCP*)sock);
            }
            return 0;
        }
        case SO_RCVBUF: {
            const unsigned int* val =
                process_getReadablePtr(sys->process, sys->thread, optvalPtr, sizeof(int));
            size_t newsize =
                (*val) * 2; // Linux kernel doubles this value upon setting
            socket_setInputBufferSize(sock, newsize);
            if (descriptor_getType((Descriptor*)sock) == DT_TCPSOCKET) {
                tcp_disableReceiveBufferAutotuning((TCP*)sock);
            }
            return 0;
        }
        case SO_REUSEADDR: {
            // TODO implement this, tor and tgen use it
            debug("setsockopt SO_REUSEADDR not yet implemented");
            return 0;
        }
#ifdef SO_REUSEPORT
        case SO_REUSEPORT: {
            // TODO implement this, tgen uses it
            debug("setsockopt SO_REUSEPORT not yet implemented");
            return 0;
        }
#endif
        case SO_KEEPALIVE: {
            // TODO implement this, libevent uses it in
            // evconnlistener_new_bind()
            debug("setsockopt SO_KEEPALIVE not yet implemented");
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
    debug("trying to recv %zu bytes on socket %i", bufSize, sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);

    Descriptor* desc = (Descriptor*)socket_desc;
    if (errcode < 0 && _syscallhandler_readableWhenClosed(sys, desc)) {
        errcode = 0;
    }

    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    if (srcAddrPtr.val && !addrlenPtr.val) {
        info("Cannot get from address with NULL address length info.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    if (flags & ~MSG_DONTWAIT) {
        warning("Unsupported recv flag(s): %d", flags);
    }

    ssize_t retval = 0;

    if (descriptor_getType(desc) == DT_TCPSOCKET) {
        int errcode = tcp_getConnectionError((TCP*)socket_desc);

        if (errcode > 0) {
            /* connect() was not called yet. */
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOTCONN};
        } else if (errcode == -EALREADY) {
            /* Connection in progress. */
            retval = -EWOULDBLOCK;
        }
    }

    struct sockaddr_in inet_addr = {.sin_family = AF_INET};

    if (retval == 0) {
        size_t sizeNeeded = bufSize;

        if (descriptor_getType(desc) == DT_TCPSOCKET) {
            // we can only truncate the data if it is a TCP connection
            /* TODO: Dynamically compute size based on how much data is actually
             * available in the descriptor. */
            sizeNeeded = MIN(sizeNeeded, SYSCALL_IO_BUFSIZE);
        } else if (descriptor_getType(desc) == DT_UDPSOCKET) {
            // allow it to be 1 byte longer than the max datagram size
            sizeNeeded = MIN(sizeNeeded, CONFIG_DATAGRAM_MAX_SIZE + 1);
        }

        void* buf = NULL;
        if (bufPtr.val) {
            // if sizeNeeded is 0, process_getWriteablePtr() will always return a null pointer
            buf = process_getWriteablePtr(sys->process, sys->thread, bufPtr, sizeNeeded);
        }

        retval = transport_receiveUserData((Transport*)socket_desc, buf, sizeNeeded,
                                               &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);

        debug("recv returned %zd", retval);
    }

    bool nonblocking_mode = descriptor_getFlags(desc) & O_NONBLOCK || flags & MSG_DONTWAIT;
    if (retval == -EWOULDBLOCK && !nonblocking_mode) {
        debug("recv would block on socket %i", sockfd);
        /* We need to block until the descriptor is ready to read. */
        Trigger trigger = (Trigger){
            .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_READABLE};
        return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
    }

    /* check if they wanted to know where we got the data from */
    if (retval > 0 && srcAddrPtr.val) {
        debug("address info is requested in recv on socket %i", sockfd);
        _syscallhandler_getnameHelper(sys, &inet_addr, srcAddrPtr, addrlenPtr);
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)retval};
}

SysCallReturn _syscallhandler_sendtoHelper(SysCallHandler* sys, int sockfd,
                                           PluginPtr bufPtr, size_t bufSize,
                                           int flags, PluginPtr destAddrPtr,
                                           socklen_t addrlen) {
    debug("trying to send %zu bytes on socket %i", bufSize, sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Need non-NULL buffer. */
    if (!bufPtr.val) {
        info("Can't send from NULL buffer on socket %i", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* TODO: when we support AF_UNIX this could be sockaddr_un */
    size_t inet_len = sizeof(struct sockaddr_in);
    if (destAddrPtr.val && addrlen < inet_len) {
        info("Address length %ld is too small on socket %i", (long int)addrlen,
             sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    if (flags & ~MSG_DONTWAIT) {
        warning("Unsupported send flag(s): %d", flags);
    }

    /* Get the address info if they specified one. */
    in_addr_t dest_ip = 0;
    in_port_t dest_port = 0;

    if (destAddrPtr.val) {
        const struct sockaddr* dest_addr =
            process_getReadablePtr(sys->process, sys->thread, destAddrPtr, addrlen);
        utility_assert(dest_addr);

        /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
        if (dest_addr->sa_family != AF_INET) {
            warning(
                "We only support address family AF_INET on socket %i", sockfd);
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = -EAFNOSUPPORT};
        }

        dest_ip = ((struct sockaddr_in*)dest_addr)->sin_addr.s_addr;
        dest_port = ((struct sockaddr_in*)dest_addr)->sin_port;
    }

    Descriptor* desc = (Descriptor*)socket_desc;
    errcode = 0;

    if (descriptor_getType(desc) == DT_UDPSOCKET) {
        /* make sure that we have somewhere to send it */
        if (dest_ip == 0 || dest_port == 0) {
            /* its ok if they setup a default destination with connect() */
            socket_getPeerName(socket_desc, &dest_ip, &dest_port);
            if (dest_ip == 0 || dest_port == 0) {
                /* we have nowhere to send it */
                return (SysCallReturn){
                    .state = SYSCALL_DONE, .retval.as_i64 = -EDESTADDRREQ};
            }
        }

        /* if this socket is not bound, do an implicit bind to a random port */
        if (!socket_isBound(socket_desc)) {
            ProtocolType ptype = socket_getProtocol(socket_desc);

            /* We don't bind to peer ip/port since that might change later. */
            in_addr_t bindAddr =
                (dest_ip == htonl(INADDR_LOOPBACK))
                    ? htonl(INADDR_LOOPBACK)
                    : address_toNetworkIP(host_getDefaultAddress(sys->host));
            in_port_t bindPort =
                host_getRandomFreePort(sys->host, ptype, bindAddr, 0, 0);

            if (!bindPort) {
                return (SysCallReturn){
                    .state = SYSCALL_DONE, .retval.as_i64 = -EADDRNOTAVAIL};
            }

            /* bind port and set netiface->socket associations */
            host_associateInterface(
                sys->host, socket_desc, bindAddr, bindPort, 0, 0);
        }
    } else { // DT_TCPSOCKET
        errcode = tcp_getConnectionError((TCP*)socket_desc);

        debug("connection error state is currently %i", errcode);

        if (errcode > 0) {
            /* connect() was not called yet.
             * TODO: Can they can piggy back a connect() on sendto() if they
             * provide an address for the connection? */
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = -ENOTCONN};
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

        if (descriptor_getType(desc) == DT_TCPSOCKET) {
            // we can only truncate the data if it is a TCP connection
            /* TODO: Dynamically compute size based on how much data is actually
             * available in the descriptor. */
            sizeNeeded = MIN(sizeNeeded, SYSCALL_IO_BUFSIZE);
        } else if (descriptor_getType(desc) == DT_UDPSOCKET) {
            // allow it to be 1 byte longer than the max so that we can receive EMSGSIZE
            sizeNeeded = MIN(sizeNeeded, CONFIG_DATAGRAM_MAX_SIZE + 1);
        }

        const void* buf = process_getReadablePtr(sys->process, sys->thread, bufPtr, sizeNeeded);

        retval = transport_sendUserData(
            (Transport*)socket_desc, buf, sizeNeeded, dest_ip, dest_port);

        debug("send returned %zd", retval);
    }

    bool nonblocking_mode = descriptor_getFlags(desc) & O_NONBLOCK || flags & MSG_DONTWAIT;
    if (retval == -EWOULDBLOCK && !nonblocking_mode) {
        if (bufSize > 0) {
            /* We need to block until the descriptor is ready to write. */
            Trigger trigger = (Trigger){
                .type = TRIGGER_DESCRIPTOR, .object = desc, .status = STATUS_DESCRIPTOR_WRITABLE};
            return (SysCallReturn){.state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
        } else {
            /* We attempted to write 0 bytes, so no need to block or return EWOULDBLOCK. */
            retval = 0;
        }
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = (int64_t)retval};
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

    debug("trying to bind on socket %i", sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    /* It's an error if it is already bound. */
    if (socket_isBound(socket_desc)) {
        info("socket descriptor %i is already bound to an address", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if (addrlen < inet_len) {
        info("supplied address is not large enough for a inet address");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Make sure the addr PluginPtr is not NULL. */
    if (!addrPtr.val) {
        info("binding to a NULL address is invalid");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    const struct sockaddr* addr =
        process_getReadablePtr(sys->process, sys->thread, addrPtr, addrlen);
    utility_assert(addr);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if (addr->sa_family != AF_INET) {
        warning("binding to address family %i, but we only support AF_INET",
                (int)addr->sa_family);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get the requested address and port. */
    struct sockaddr_in* inet_addr = (struct sockaddr_in*)addr;
    in_addr_t bindAddr = inet_addr->sin_addr.s_addr;
    in_port_t bindPort = inet_addr->sin_port;

    errcode =
        _syscallhandler_bindHelper(sys, socket_desc, bindAddr, bindPort, 0, 0);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

SysCallReturn syscallhandler_connect(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    PluginPtr addrPtr = args->args[1].as_ptr; // const struct sockaddr*
    socklen_t addrlen = args->args[2].as_u64;

    debug("trying to connect on socket %i", sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    /* Make sure the addr PluginPtr is not NULL. */
    if (!addrPtr.val) {
        info("connecting to a NULL address is invalid");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if (addrlen < inet_len) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    const struct sockaddr* addr =
        process_getReadablePtr(sys->process, sys->thread, addrPtr, addrlen);
    utility_assert(addr);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if (addr->sa_family != AF_INET && addr->sa_family != AF_UNSPEC) {
        warning("connecting to address family %i, but we only support AF_INET",
                (int)addr->sa_family);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EAFNOSUPPORT};
    } else if (!socket_isFamilySupported(socket_desc, addr->sa_family)) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EAFNOSUPPORT};
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
        Address* myAddress = host_getDefaultAddress(sys->host);
        Address* peerAddress = worker_resolveIPToAddress(peerAddr);
        if (!peerAddress || !topology_isRoutable(
                                worker_getTopology(), myAddress, peerAddress)) {
            /* can't route it - there is no node with this address */
            gchar* peerAddressString = address_ipToNewString(peerAddr);
            warning("attempting to connect to address '%s:%u' for which no "
                    "host exists",
                    peerAddressString, ntohs(peerPort));
            g_free(peerAddressString);
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = -ECONNREFUSED};
        }
    }

    if (!socket_isBound(socket_desc)) {
        /* do an implicit bind to a random ephemeral port.
         * use default interface unless the remote peer is on loopback */
        in_addr_t bindAddr = (loopbackAddr == peerAddr)
                                 ? loopbackAddr
                                 : host_getDefaultIP(sys->host);
        errcode = _syscallhandler_bindHelper(
            sys, socket_desc, bindAddr, 0, peerAddr, peerPort);
        if (errcode < 0) {
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = errcode};
        }
    }

    /* Now we are ready to connect. */
    errcode = socket_connectToPeer(socket_desc, peerAddr, peerPort, family);

    Descriptor* desc = (Descriptor*)socket_desc;
    if (descriptor_getType(desc) == DT_TCPSOCKET &&
        !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking connect call. */
        if (errcode == -EINPROGRESS) {
            /* This is the first time we ever called connect, and so we
             * need to wait for the 3-way handshake to complete.
             * We will wait indefinitely for a success or failure. */
            Trigger trigger =
                (Trigger){.type = TRIGGER_DESCRIPTOR,
                          .object = desc,
                          .status = STATUS_DESCRIPTOR_ACTIVE | STATUS_DESCRIPTOR_WRITABLE};
            return (SysCallReturn){
                .state = SYSCALL_BLOCK, .cond = syscallcondition_new(trigger, NULL)};
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
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

SysCallReturn syscallhandler_getpeername(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;

    debug("trying to get peer name on socket %i", sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    // TODO I'm not sure if we should be able to get the peer name on UDP
    // sockets. If you call connect on it, then getpeername should probably
    // return the peer you associated in the most recent connect call.
    // If we can validate that, we can delete this comment.
    //    /* Only a TCP socket can be connected to a peer.
    //     * TODO: Needs to be updated when we support AF_UNIX. */
    //    DescriptorType type = descriptor_getType((Descriptor*)socket_desc);
    //    if(type != DT_TCPSOCKET) {
    //        info("descriptor %i is not a TCP socket", sockfd);
    //        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 =
    //        -ENOTCONN};
    //    }

    /* Get the name of the connected peer.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    gboolean hasName = socket_getPeerName(
        socket_desc, &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);
    if (!hasName) {
        info("Socket %i has no peer name.", sockfd);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -ENOTCONN};
    }

    /* Use helper to write out the result. */
    return _syscallhandler_getnameHelper(
        sys, &inet_addr, args->args[1].as_ptr, args->args[2].as_ptr);
}

SysCallReturn syscallhandler_getsockname(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;

    debug("trying to get sock name on socket %i", sockfd);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    /* Get the name of the socket.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    gboolean hasName = socket_getSocketName(
        socket_desc, &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);
    /* If !hasName, leave sin_addr and sin_port at their default 0 values. */

    /* If we are bound to INADDR_ANY, we should instead return the address used
     * to communicate with the connected peer (if we have one). */
    if (inet_addr.sin_addr.s_addr == htonl(INADDR_ANY)) {
        in_addr_t peerIP = 0;
        if (socket_getPeerName(socket_desc, &peerIP, NULL) &&
            peerIP != htonl(INADDR_LOOPBACK)) {
            inet_addr.sin_addr.s_addr = host_getDefaultIP(sys->host);
        }
    }

    /* Use helper to write out the result. */
    return _syscallhandler_getnameHelper(
        sys, &inet_addr, args->args[1].as_ptr, args->args[2].as_ptr);
}

SysCallReturn syscallhandler_getsockopt(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int level = args->args[1].as_i64;
    int optname = args->args[2].as_i64;
    PluginPtr optvalPtr = args->args[3].as_ptr; // void*
    PluginPtr optlenPtr = args->args[4].as_ptr; // socklen_t*

    debug("trying to getsockopt on socket %i at level %i for opt %i", sockfd,
          level, optname);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    /* The pointers must be non-null. */
    if (!optvalPtr.val || !optlenPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    errcode = 0;
    switch (level) {
        case SOL_TCP: {
            if (descriptor_getType((Descriptor*)socket_desc) != DT_TCPSOCKET) {
                errcode = -EINVAL;
                break;
            }

            errcode = _syscallhandler_getTCPOptHelper(
                sys, (TCP*)socket_desc, optname, optvalPtr, optlenPtr);
            break;
        }
        case SOL_SOCKET: {
            errcode = _syscallhandler_getSocketOptHelper(
                sys, socket_desc, optname, optvalPtr, optlenPtr);
            break;
        }
        default:
            warning("getsockopt called with unsupported level %i", level);
            errcode = -ENOPROTOOPT;
            break;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

SysCallReturn syscallhandler_listen(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int backlog = args->args[1].as_i64;

    debug("trying to listen on socket %i", sockfd);

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errcode =
        _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(tcp_desc);

    /* only listen on the socket if it is not used for other functions */
    if (!tcp_isListeningAllowed(tcp_desc)) {
        info("Cannot listen on previously used socket %i", sockfd);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EOPNOTSUPP};
    }

    /* if we are already listening, just return 0.
     * linux may actually update the backlog to the new backlog passed into this
     * function, but we currently do not make use of the backlog. */
    if (tcp_isValidListener(tcp_desc)) {
        debug("Socket %i already set up as a listener", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE};
    }

    /* We are allowed to listen but not already listening, start now. */
    if (!socket_isBound((Socket*)tcp_desc)) {
        /* Implicit bind: bind to all interfaces at an ephemeral port. */
        debug("Implicitly binding listener socket %i", sockfd);
        errcode = _syscallhandler_bindHelper(
            sys, (Socket*)tcp_desc, htonl(INADDR_ANY), 0, 0, 0);
        if (errcode < 0) {
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = errcode};
        }
    }

    tcp_enterServerMode(tcp_desc, backlog);
    return (SysCallReturn){.state = SYSCALL_DONE};
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

    debug("trying to setsockopt on socket %i at level %i for opt %i", sockfd,
          level, optname);

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errcode =
        _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }
    utility_assert(socket_desc);

    /* The pointers must be non-null. */
    if (!optvalPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    errcode = 0;
    switch (level) {
        case SOL_SOCKET: {
            errcode = _syscallhandler_setSocketOptHelper(
                sys, socket_desc, optname, optvalPtr, optlen);
            break;
        }
        default:
            warning("setsockopt called with unsupported level %i", level);
            errcode = -ENOPROTOOPT;
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

SysCallReturn syscallhandler_shutdown(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    int sockfd = args->args[0].as_i64;
    int how = args->args[1].as_i64;

    debug("trying to shutdown on socket %i with how %i", sockfd, how);
    
    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        info("invalid how %i", how);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and validate the socket. */
    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    TCP* tcp_desc = NULL;
    errcode = _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    if (errcode >= 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = tcp_shutdown(tcp_desc, how)};
    }

    UDP* udp_desc = NULL;
    errcode = _syscallhandler_validateUDPSocketHelper(sys, sockfd, &udp_desc);
    if (errcode >= 0) {
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = udp_shutdown(udp_desc, how)};
    }

    warning("socket %d is neither a TCP nor UDP socket", sockfd);
    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOTCONN};
}

SysCallReturn syscallhandler_socket(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int domain = args->args[0].as_i64;
    int type = args->args[1].as_i64;
    int protocol = args->args[2].as_i64;

    debug("trying to create new socket");

    /* Remove the two possible flags to get the type. */
    int type_no_flags = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    /* TODO: add support for AF_UNIX? */
    /* The below are warnings so the Shadow user knows that we don't support
     * everything that Linux supports. */
    if (domain != AF_INET) {
        warning("unsupported socket domain \"%i\", we only support AF_INET",
                domain);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EAFNOSUPPORT};
    } else if (type_no_flags != SOCK_STREAM && type_no_flags != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM "
                "and SOCK_DGRAM",
                type_no_flags);
        /* TODO: unclear if we should return EPROTONOSUPPORT or EINVAL */
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    } else if (type_no_flags == SOCK_STREAM && protocol != 0 && protocol != IPPROTO_TCP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_TCP on sockets of type SOCK_STREAM", protocol);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    } else if (type_no_flags == SOCK_DGRAM && protocol != 0 && protocol != IPPROTO_UDP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_UDP on sockets of type SOCK_DGRAM", protocol);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    }

    /* We are all set to create the socket. */
    guint64 recvBufSize = host_getConfiguredRecvBufSize(sys->host);
    guint64 sendBufSize = host_getConfiguredSendBufSize(sys->host);

    Socket* sock_desc = NULL;
    if (type_no_flags == SOCK_STREAM) {
        sock_desc = (Socket*)tcp_new(recvBufSize, sendBufSize);
    } else {
        sock_desc = (Socket*)udp_new(recvBufSize, sendBufSize);
    }

    /* Now make sure it will be valid when we operate on it. */
    int sockfd =
        process_registerDescriptor(sys->process, &sock_desc->super.super);

    int errcode = _syscallhandler_validateSocketHelper(sys, sockfd, NULL);
    if (errcode != 0) {
        error("Unable to find socket %i that we just created.", sockfd);
    }
    utility_assert(errcode == 0);

    /* Set any options that were given. */
    if (type & SOCK_NONBLOCK) {
        descriptor_addFlags(&sock_desc->super.super, O_NONBLOCK);
    }
    if (type & SOCK_CLOEXEC) {
        descriptor_addFlags(&sock_desc->super.super, O_CLOEXEC);
    }

    debug("socket() returning fd %i", sockfd);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = sockfd};
}

SysCallReturn syscallhandler_socketpair(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int domain = args->args[0].as_i64;
    int type = args->args[1].as_i64;
    int protocol = args->args[2].as_i64;
    PluginPtr fdsPtr = args->args[3].as_ptr; // int [2]

    debug("trying to create new socketpair");

    /* Null pointer is invalid. */
    if(!fdsPtr.val) {
        debug("Null pointer is invalid.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Only AF_UNIX (i.e., AF_LOCAL) is supported. */
    if(domain != AF_UNIX) {
        debug("Domain %d not supported", domain);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EAFNOSUPPORT};
    }

    /* Remove the two possible flags to get the type. */
    int type_no_flags = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    /* The below are warnings so the Shadow user knows that we don't support
     * everything that Linux supports. */
    if (type_no_flags != SOCK_STREAM && type_no_flags != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM", type_no_flags);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    } else if (type_no_flags == SOCK_STREAM && protocol != 0 && protocol != IPPROTO_TCP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_TCP on sockets of type SOCK_STREAM", protocol);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    } else if (type_no_flags == SOCK_DGRAM && protocol != 0 && protocol != IPPROTO_UDP) {
        warning(
            "unsupported socket protocol \"%i\", we only support IPPROTO_UDP on sockets of type SOCK_DGRAM", protocol);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    }

    /* TODO: should we actually be running TCP/UDP internally (i.e., using already connected TCP/UDP sockets here) instead? */
    Channel* socketA = channel_new(CT_NONE);
    Channel* socketB = channel_new(CT_NONE);
    channel_setLinkedChannel(socketA, socketB);
    channel_setLinkedChannel(socketB, socketA);

    /* Set any options that were given. */
    if (type & SOCK_NONBLOCK) {
        descriptor_addFlags((Descriptor*)socketA, O_NONBLOCK);
        descriptor_addFlags((Descriptor*)socketB, O_NONBLOCK);
    }
    if (type & SOCK_CLOEXEC) {
        descriptor_addFlags((Descriptor*)socketA, O_CLOEXEC);
        descriptor_addFlags((Descriptor*)socketB, O_CLOEXEC);
    }

    /* Return the socket fds to the caller. */
    int* sockfd = process_getWriteablePtr(sys->process, sys->thread, fdsPtr, 2*sizeof(int));

    sockfd[0] = process_registerDescriptor(sys->process, (Descriptor*)socketA);
    sockfd[1] = process_registerDescriptor(sys->process, (Descriptor*)socketB);

    debug("Created socketpair with fd %i and fd %i", sockfd[0], sockfd[1]);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = 0};
}
