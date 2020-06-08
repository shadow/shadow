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
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/udp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
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
        (descriptor_getStatus(desc) & DS_CLOSED)) {
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
    if (sockfd <= 0) {
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
    if (type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
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
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* OK, now we can check if we have anything to accept. */
    in_addr_t addr = 0;
    in_port_t port = 0;
    int accepted_fd = 0;
    errcode = tcp_acceptServerPeer(tcp_desc, &addr, &port, &accepted_fd);

    Descriptor* desc = (Descriptor*)tcp_desc;
    if (errcode == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking accept, and we don't have a connection yet.
         * The socket becomes readable when we have a connection to accept.
         * This blocks indefinitely without a timeout. */
        debug("Listening socket %i waiting for acceptable connection.", sockfd);
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_READABLE);
        return (SysCallReturn){.state = SYSCALL_BLOCK};
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

    /* Return the accepted socket data if they passed a non-NULL addr. */
    if (addrPtr.val && addrlenPtr.val) {
        struct sockaddr_in* addr_out =
            thread_getWriteablePtr(sys->thread, addrPtr, sizeof(*addr_out));
        addr_out->sin_addr.s_addr = addr;
        addr_out->sin_port = port;
        addr_out->sin_family = AF_INET;

        socklen_t* addrlen_out = thread_getWriteablePtr(
            sys->thread, addrlenPtr, sizeof(*addrlen_out));
        *addrlen_out = sizeof(*addr_out);
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

static SysCallReturn
_syscallhandler_getnameHelper(SysCallHandler* sys,
                              struct sockaddr_in* inet_addr, PluginPtr addrPtr,
                              PluginPtr addrlenPtr) {
    if (!addrPtr.val || !addrlenPtr.val) {
        info("Cannot get name with NULL return address info.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Read the addrlen first before writing the results. We use clone here
     * because reading the addrlen PluginPtr here and then trying to write it
     * below will cause crashes in the thread backend. */
    socklen_t* addrlen_cloned =
        thread_newClonedPtr(sys->thread, addrlenPtr, sizeof(*addrlen_cloned));
    size_t sizeAvail = (size_t)*addrlen_cloned;
    thread_releaseClonedPtr(sys->thread, addrlen_cloned);

    if (sizeAvail <= 0) {
        info("Unable to store name in %zu bytes.", sizeAvail);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* The result is truncated if they didn't give us enough space. */
    size_t retSize = MIN(sizeAvail, sizeof(*inet_addr));

    /* Return the results */
    struct sockaddr* addr =
        thread_getWriteablePtr(sys->thread, addrPtr, retSize);
    socklen_t* addrlen =
        thread_getWriteablePtr(sys->thread, addrlenPtr, sizeof(*addrlen));

    memcpy(addr, inet_addr, retSize);
    *addrlen = (socklen_t)retSize;

    return (SysCallReturn){.state = SYSCALL_DONE};
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

    /* Need non-NULL buffer. */
    if (!bufPtr.val) {
        info("Can't recv into NULL buffer on socket %i", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    if (!bufSize) {
        info("Invalid length %zu provided on socket %i", bufSize, sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    if (srcAddrPtr.val && !addrlenPtr.val) {
        info("Cannot get from address with NULL address length info.");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: Dynamically compute size based on how much data is actually
     * available in the descriptor. */
    size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
    void* buf = thread_getWriteablePtr(sys->thread, bufPtr, sizeNeeded);
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};

    ssize_t retval = transport_receiveUserData(
        (Transport*)socket_desc, buf, sizeNeeded, &inet_addr.sin_addr.s_addr,
        &inet_addr.sin_port);

    debug("recv returned %zd", retval);

    if (retval == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        debug("recv would block on socket %i", sockfd);
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_READABLE);
        return (SysCallReturn){.state = SYSCALL_BLOCK};
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

    if (!bufSize) {
        info("Invalid buf length %zu provided on socket %i", bufSize, sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* TODO: when we support AF_UNIX this could be sockaddr_un */
    size_t inet_len = sizeof(struct sockaddr_in);
    if (destAddrPtr.val && addrlen < inet_len) {
        info("Address length %ld is too small on socket %i", (long int)addrlen,
             sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get the address info if they specified one. */
    in_addr_t dest_ip = 0;
    in_port_t dest_port = 0;

    if (destAddrPtr.val) {
        const struct sockaddr* dest_addr =
            thread_getReadablePtr(sys->thread, destAddrPtr, addrlen);
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
        /* TODO: Dynamically compute size based on how much data is actually
         * available in the descriptor. */
        size_t sizeNeeded = MIN(bufSize, SYSCALL_IO_BUFSIZE);
        const void* buf =
            thread_getReadablePtr(sys->thread, bufPtr, sizeNeeded);

        retval = transport_sendUserData(
            (Transport*)socket_desc, buf, sizeNeeded, dest_ip, dest_port);

        debug("send returned %zd", retval);
    }

    if (retval == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* We need to block until the descriptor is ready to read. */
        process_listenForStatus(
            sys->process, sys->thread, NULL, desc, DS_WRITABLE);
        return (SysCallReturn){.state = SYSCALL_BLOCK};
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
        thread_getReadablePtr(sys->thread, addrPtr, addrlen);
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

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if (addrlen < inet_len) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Make sure the addr PluginPtr is not NULL. */
    if (!addrPtr.val) {
        info("connecting to a NULL address is invalid");
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

    const struct sockaddr* addr =
        thread_getReadablePtr(sys->thread, addrPtr, addrlen);
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
            process_listenForStatus(
                sys->process, sys->thread, NULL, desc, DS_WRITABLE);
            return (SysCallReturn){.state = SYSCALL_BLOCK};
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
    if (!hasName) {
        /* TODO: check what Linux returns on new, unbound socket. The man
         * page does not specify; using EINVAL for now. */
        info("Unbound socket %i has no name.", sockfd);
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EINVAL};
    }

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
    TCP* tcp_desc = NULL;
    int errcode =
        _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    if (errcode < 0) {
        if (errcode == -EOPNOTSUPP) {
            errcode = -ENOTCONN;
        }
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = (int64_t)tcp_shutdown(tcp_desc, how)};
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
    } else if (protocol != 0) {
        warning(
            "unsupported socket protocol \"%i\", we only support 0", protocol);
        return (SysCallReturn){
            .state = SYSCALL_DONE, .retval.as_i64 = -EPROTONOSUPPORT};
    }

    /* We are all set to create the socket. */
    guint64 recvBufSize = host_getConfiguredRecvBufSize(sys->host);
    guint64 sendBufSize = host_getConfiguredSendBufSize(sys->host);

    Socket* sock_desc = NULL;
    if(type_no_flags == SOCK_STREAM) {
        sock_desc = (Socket*)tcp_new(recvBufSize, sendBufSize);
    } else {
        sock_desc = (Socket*)udp_new(recvBufSize, sendBufSize);
    }

    /* Now make sure it will be valid when we operate on it. */
    int sockfd = process_registerDescriptor(sys->process, &sock_desc->super.super);

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
