/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/socket.h"

#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall_handler.h"
#include "main/host/thread.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

#define SYSCALL_RETURN_IF_ERROR(errcode)                                       \
    if(errcode < 0) {                                                          \
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE,                   \
                               .retval.as_i64 = errcode};                      \
    }

static int _syscallhandler_validateSocketHelper(SysCallHandler* sys, int sockfd, Socket** sock_desc_out) {
    /* Check that fd is within bounds. */
    if (sockfd <= 0) {
        info("descriptor %i out of bounds", sockfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, sockfd);
    int errorCode = _syscallhandler_validateDescriptor(desc, DT_NONE);

    if(errorCode) {
        info("descriptor %i is invalid", sockfd);
        return errorCode;
    }

    DescriptorType type = descriptor_getType(desc);
    if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
        info("descriptor %i with type %i is not a socket", sockfd, (int)type);
        return -ENOTSOCK;
    }

    /* Now we know we have a valid socket. */
    if(sock_desc_out) {
        *sock_desc_out = (Socket*) desc;
    }
    return 0;
}

static int _syscallhandler_validateTCPSocketHelper(SysCallHandler* sys, int sockfd, TCP** tcp_desc_out) {
    Socket* sock_desc = NULL;
    int errorCode = _syscallhandler_validateSocketHelper(sys, sockfd, &sock_desc);

    if(errorCode) {
        return errorCode;
    }

    DescriptorType type = descriptor_getType((Descriptor*)sock_desc);
    if(type != DT_TCPSOCKET) {
        info("descriptor %i is not a TCP socket", sockfd);
        return -EOPNOTSUPP;
    }

    /* Now we know we have a valid TCP socket. */
    if(tcp_desc_out) {
        *tcp_desc_out = (TCP*) sock_desc;
    }
    return 0;
}

static SysCallReturn _syscallhandler_acceptHelper(SysCallHandler* sys,
                                                  int sockfd, PluginPtr addrPtr,
                                                  PluginPtr addrlenPtr,
                                                  int flags) {
    /* Check that non-valid flags are not given. */
    if(flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
        info("invalid flags \"%i\", only SOCK_NONBLOCK and SOCK_CLOEXEC are allowed",
                        flags);
        return (SysCallReturn){
                    .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errorCode = _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);

    if(errorCode < 0) {
        return (SysCallReturn){
                    .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
    }
    utility_assert(tcp_desc);

    /* We must be listening in order to accept. */
    if(!tcp_isValidListener(tcp_desc)) {
        info("socket %i is not listening", sockfd);
        return (SysCallReturn){
                    .state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EINVAL};
    }

    /* Make sure they supplied addrlen if they requested an addr. */
    if(addrPtr.val && !addrlenPtr.val) {
        info("addrlen was NULL when addr was non-NULL");
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE, .retval.as_i64 = -EINVAL};
    }

    /* OK, now we can check if we have anything to accept. */
    in_addr_t addr = 0;
    in_port_t port = 0;
    int accepted_fd = 0;
    errorCode = tcp_acceptServerPeer(tcp_desc, &addr, &port, &accepted_fd);

    Descriptor* desc = (Descriptor*) tcp_desc;
    if(errorCode == -EWOULDBLOCK && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking accept, and we don't have a connection yet.
         * The socket becomes readable when we have a connection to accept.
         * This blocks indefinitely without a timeout. */
        debug("Listening socket %i waiting for acceptable connection.", sockfd);
        process_listenForStatus(sys->process, sys->thread, NULL, desc, DS_READABLE);
        return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
    } else if(errorCode < 0) {
        debug("TCP error when accepting connection on socket %i", sockfd);
        return (SysCallReturn){
                    .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
    }

    /* We accepted something! */
    utility_assert(accepted_fd > 0);
    TCP* accepted_tcp_desc = NULL;
    errorCode = _syscallhandler_validateTCPSocketHelper(sys, accepted_fd, &accepted_tcp_desc);
    utility_assert(errorCode == 0);

    debug("listening socket %i accepted fd %i", sockfd, accepted_fd);

    /* Set the flags on the accepted socket if requested. */
    if (flags & SOCK_NONBLOCK) {
        descriptor_addFlags((Descriptor*)accepted_tcp_desc, O_NONBLOCK);
    }
    if (flags & SOCK_CLOEXEC) {
        descriptor_addFlags((Descriptor*)accepted_tcp_desc, O_CLOEXEC);
    }

    /* Return the accepted socket data if they passed a non-NULL addr. */
    if(addrPtr.val && addrlenPtr.val) {
        struct sockaddr_in *addr_out = thread_getWriteablePtr(sys->thread, addrPtr, sizeof(*addr_out));
        addr_out->sin_addr.s_addr = addr;
        addr_out->sin_port = port;
        addr_out->sin_family = AF_INET;

        socklen_t *addrlen_out = thread_getWriteablePtr(sys->thread, addrlenPtr, sizeof(*addrlen_out));
        *addrlen_out = sizeof(*addr_out);
    }
    return (SysCallReturn){
                        .state = SYSCALL_RETURN_DONE, .retval.as_i64 = accepted_fd};
}


static int _syscallhandler_bindHelper(SysCallHandler* sys, Socket* socket_desc,
        in_addr_t addr, in_port_t port, in_addr_t peerAddr, in_port_t peerPort) {
#ifdef DEBUG
    gchar* bindAddrStr = address_ipToNewString(addr);
    gchar* peerAddrStr = address_ipToNewString(peerAddr);
    debug("trying to bind to inet address %s:%u on socket %i with peer %s:%u",
            bindAddrStr, ntohs(port), descriptor_getHandle((Descriptor*)socket_desc), peerAddrStr, ntohs(peerPort));
    g_free(bindAddrStr);
    g_free(peerAddrStr);
#endif

    /* make sure we have an interface at that address */
    if(!host_doesInterfaceExist(sys->host, addr)) {
        info("no network interface exists for the provided bind address");
        return -EINVAL;
    }

    /* Each protocol type gets its own ephemeral port mapping. */
    ProtocolType ptype = socket_getProtocol(socket_desc);

    /* Get a free ephemeral port if they didn't specify one. */
    if(port == 0) {
        port = host_getRandomFreePort(sys->host, ptype, addr, peerAddr, peerPort);
        debug("binding to generated ephemeral port %u", ntohs(port));
    }

    /* Ephemeral port unavailable. */
    if(port == 0) {
        info("binding required an ephemeral port and none are available");
        return -EADDRINUSE;
    }

    /* Make sure the port is available at this address for this protocol. */
    if(!host_isInterfaceAvailable(sys->host, ptype, addr, port, peerAddr, peerPort)) {
        info("the provided address and port %u are not available", ntohs(port));
        return -EADDRINUSE;
    }

    /* bind port and set associations */
    host_associateInterface(sys->host, socket_desc, addr, port, peerAddr, peerPort);
    return 0;
}

static SysCallReturn _syscallhandler_getnameHelper(SysCallHandler* sys, struct sockaddr_in *inet_addr, PluginPtr addrPtr, PluginPtr addrlenPtr) {
    if(!addrPtr.val || !addrlenPtr.val) {
        info("Cannot get name with NULL return address info.");
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* Read the addrlen first before writing the results. We use clone here
     * because reading the addrlen PluginPtr here and then trying to write it
     * below will cause crashes in the thread backend. */
    socklen_t* addrlen_cloned = thread_newClonedPtr(sys->thread, addrlenPtr, sizeof(*addrlen_cloned));
    size_t sizeAvail = (size_t)*addrlen_cloned;
    thread_releaseClonedPtr(sys->thread, addrlen_cloned);

    if(sizeAvail <= 0) {
        info("Unable to store name in %zu bytes.", sizeAvail);
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* The result is truncated if they didn't give us enough space. */
    size_t retSize = MIN(sizeAvail, sizeof(*inet_addr));

    /* Return the results */
    struct sockaddr* addr = thread_getWriteablePtr(sys->thread, addrPtr, retSize);
    socklen_t* addrlen = thread_getWriteablePtr(sys->thread, addrlenPtr, sizeof(*addrlen));

    memcpy(addr, inet_addr, retSize);
    *addrlen = (socklen_t)retSize;

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE};
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
    const struct sockaddr* addr; // args->args[1]
    socklen_t addrlen = (socklen_t)args->args[2].as_u64;

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errorCode = _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    SYSCALL_RETURN_IF_ERROR(errorCode);
    utility_assert(socket_desc);

    /* It's an error if it is already bound. */
    if(socket_isBound(socket_desc)) {
        info("socket descriptor %i is already bound to an address", sockfd);
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if(addrlen < inet_len) {
        info("supplied address is not large enough for a inet address");
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* Make sure the addr PluginPtr is not NULL. */
    if(!args->args[1].as_ptr.val) {
        info("binding to a NULL address is invalid");
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    addr = thread_getReadablePtr(sys->thread, args->args[1].as_ptr, addrlen);
    utility_assert(addr);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if(addr->sa_family != AF_INET) {
        warning("binding to address family %i, but we only support AF_INET", (int)addr->sa_family);
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* Get the requested address and port. */
    struct sockaddr_in* inet_addr = (struct sockaddr_in*) addr;
    in_addr_t bindAddr = inet_addr->sin_addr.s_addr;
    in_port_t bindPort = inet_addr->sin_port;

    errorCode = _syscallhandler_bindHelper(sys, socket_desc, bindAddr, bindPort, 0, 0);
    return (SysCallReturn){
                    .state = SYSCALL_RETURN_DONE, .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_connect(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int sockfd = (int)args->args[0].as_i64;
    const struct sockaddr* addr; // args->args[1]
    socklen_t addrlen = (socklen_t)args->args[2].as_u64;

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errorCode = _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    SYSCALL_RETURN_IF_ERROR(errorCode);
    utility_assert(socket_desc);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    // size_t unix_len = sizeof(struct sockaddr_un); // if sa_family==AF_UNIX
    size_t inet_len = sizeof(struct sockaddr_in);
    if(addrlen < inet_len) {
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* Make sure the addr PluginPtr is not NULL. */
    if(!args->args[1].as_ptr.val) {
        info("connecting to a NULL address is invalid");
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    addr = thread_getReadablePtr(sys->thread, args->args[1].as_ptr, addrlen);
    utility_assert(addr);

    /* TODO: we assume AF_INET here, change this when we support AF_UNIX */
    if(addr->sa_family != AF_INET && addr->sa_family != AF_UNSPEC) {
        warning("connecting to address family %i, but we only support AF_INET", (int)addr->sa_family);
        SYSCALL_RETURN_IF_ERROR(-EAFNOSUPPORT);
    } else if(!socket_isFamilySupported(socket_desc, addr->sa_family)) {
        SYSCALL_RETURN_IF_ERROR(-EAFNOSUPPORT);
    }

    /* TODO: update for AF_UNIX */
    struct sockaddr_in* inet_addr = (struct sockaddr_in*) addr;

    sa_family_t family = inet_addr->sin_family;
    in_addr_t peerAddr = inet_addr->sin_addr.s_addr;
    in_port_t peerPort = inet_addr->sin_port;
    in_addr_t loopbackAddr = htonl(INADDR_LOOPBACK);

    if(peerAddr == htonl(INADDR_ANY)) {
        peerAddr = loopbackAddr;
    }

    /* make sure we will be able to route this later */
    if(peerAddr != loopbackAddr) {
        Address* myAddress = host_getDefaultAddress(sys->host);
        Address* peerAddress = worker_resolveIPToAddress(peerAddr);
        if(!peerAddress || !topology_isRoutable(worker_getTopology(), myAddress, peerAddress)) {
            /* can't route it - there is no node with this address */
            gchar* peerAddressString = address_ipToNewString(peerAddr);
            warning("attempting to connect to address '%s:%u' for which no host exists", peerAddressString, ntohs(peerPort));
            g_free(peerAddressString);
            SYSCALL_RETURN_IF_ERROR(-ECONNREFUSED);
        }
    }

    if(!socket_isBound(socket_desc)) {
        /* do an implicit bind to a random ephemeral port.
         * use default interface unless the remote peer is on loopback */
        in_addr_t bindAddr = (loopbackAddr == peerAddr) ? loopbackAddr : host_getDefaultIP(sys->host);
        errorCode = _syscallhandler_bindHelper(sys, socket_desc, bindAddr, 0, peerAddr, peerPort);
        SYSCALL_RETURN_IF_ERROR(errorCode);
    }

    /* Now we are ready to connect. */
    errorCode = socket_connectToPeer(socket_desc, peerAddr, peerPort, family);

    Descriptor* desc = (Descriptor*)socket_desc;
    if(descriptor_getType(desc) == DT_TCPSOCKET && !(descriptor_getFlags(desc) & O_NONBLOCK)) {
        /* This is a blocking connect call. */
        if(errorCode == -EINPROGRESS) {
            /* This is the first time we ever called connect, and so we
             * need to wait for the 3-way handshake to complete.
             * We will wait indefinitely for a success or failure. */
            process_listenForStatus(sys->process, sys->thread, NULL, desc, DS_WRITABLE);
            return (SysCallReturn){.state = SYSCALL_RETURN_BLOCKED};
        } else if(_syscallhandler_wasBlocked(sys) && errorCode == -EISCONN) {
            /* It was EINPROGRESS, but is now a successful blocking connect. */
            errorCode = 0;
        }
    }

    /* Make sure we return valid error codes for connect. */
    if(errorCode == -ECONNRESET || errorCode == -ENOTCONN) {
        errorCode = -EISCONN;
    }
    /* -EALREADY is well defined in man page, but Linux returns -EINPROGRESS. */
    else if(errorCode == -EALREADY) {
        errorCode = -EINPROGRESS;
    }

    /* Return 0, -EINPROGRESS, etc. now. */
    return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                           .retval.as_i64 = errorCode};
}

SysCallReturn syscallhandler_getpeername(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = (int)args->args[0].as_i64;

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errorCode = _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    SYSCALL_RETURN_IF_ERROR(errorCode);
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
//        SYSCALL_RETURN_IF_ERROR(-ENOTCONN);
//    }

    /* Get the name of the connected peer.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    gboolean hasName = socket_getPeerName(socket_desc, &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);
    if(!hasName) {
        info("Socket %i has no peer name.", sockfd);
        SYSCALL_RETURN_IF_ERROR(-ENOTCONN);
    }

    /* Use helper to write out the result. */
    return _syscallhandler_getnameHelper(sys, &inet_addr, args->args[1].as_ptr, args->args[2].as_ptr);
}

SysCallReturn syscallhandler_getsockname(SysCallHandler* sys,
                                         const SysCallArgs* args) {
    int sockfd = (int)args->args[0].as_i64;

    /* Get and validate the socket. */
    Socket* socket_desc = NULL;
    int errorCode = _syscallhandler_validateSocketHelper(sys, sockfd, &socket_desc);
    SYSCALL_RETURN_IF_ERROR(errorCode);
    utility_assert(socket_desc);

    /* Get the name of the socket.
     * TODO: Needs to be updated when we support AF_UNIX. */
    struct sockaddr_in inet_addr = {.sin_family = AF_INET};
    gboolean hasName = socket_getSocketName(socket_desc, &inet_addr.sin_addr.s_addr, &inet_addr.sin_port);
    if(!hasName) {
        /* TODO: check what Linux returns on new, unbound socket. The man
         * page does not specify; using EINVAL for now. */
        info("Unbound socket %i has no name.", sockfd);
        SYSCALL_RETURN_IF_ERROR(-EINVAL);
    }

    /* If we are bound to INADDR_ANY, we should instead return the address used
     * to communicate with the connected peer (if we have one). */
    if(inet_addr.sin_addr.s_addr == htonl(INADDR_ANY)) {
        in_addr_t peerIP = 0;
        if(socket_getPeerName(socket_desc, &peerIP, NULL) &&
                peerIP != htonl(INADDR_LOOPBACK)) {
            inet_addr.sin_addr.s_addr = host_getDefaultIP(sys->host);
        }
    }

    /* Use helper to write out the result. */
    return _syscallhandler_getnameHelper(sys, &inet_addr, args->args[1].as_ptr, args->args[2].as_ptr);
}

SysCallReturn syscallhandler_listen(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int sockfd = (int)args->args[0].as_i64;
    int backlog = (int)args->args[1].as_i64;

    /* Get and validate the TCP socket. */
    TCP* tcp_desc = NULL;
    int errorCode = _syscallhandler_validateTCPSocketHelper(sys, sockfd, &tcp_desc);
    SYSCALL_RETURN_IF_ERROR(errorCode);
    utility_assert(tcp_desc);

    /* only listen on the socket if it is not used for other functions */
    if(!tcp_isListeningAllowed(tcp_desc)) {
        info("Cannot listen on previously used socket %i", sockfd);
        SYSCALL_RETURN_IF_ERROR(-EOPNOTSUPP);
    }

    /* if we are already listening, just return 0.
     * linux may actually update the backlog to the new backlog passed into this function,
     * but we currently do not make use of the backlog. */
    if(tcp_isValidListener(tcp_desc)) {
        debug("Socket %i already set up as a listener", sockfd);
        return (SysCallReturn){.state = SYSCALL_RETURN_DONE};
    }

    /* We are allowed to listen but not already listening, start now. */
    if(!socket_isBound((Socket*)tcp_desc)) {
        /* Implicit bind: bind to all interfaces at an ephemeral port. */
        debug("Implicitly binding listener socket %i", sockfd);
        errorCode = _syscallhandler_bindHelper(sys, (Socket*)tcp_desc, htonl(INADDR_ANY), 0, 0, 0);
        SYSCALL_RETURN_IF_ERROR(errorCode);
    }

    tcp_enterServerMode(tcp_desc, backlog);
    return (SysCallReturn){.state = SYSCALL_RETURN_DONE};
}

SysCallReturn syscallhandler_socket(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int domain = (int)args->args[0].as_i64;
    int type = (int)args->args[1].as_i64;
    int protocol = (int)args->args[2].as_i64;

    /* Remove the two possible flags to get the type. */
    int type_no_flags = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);

    /* TODO: add support for AF_UNIX? */
    /* The below are warnings so the Shadow user knows that we don't support
     * everything that Linux supports. */
    if (domain != AF_INET) {
        warning("unsupported socket domain \"%i\", we only support AF_INET",
                domain);
        SYSCALL_RETURN_IF_ERROR(-EAFNOSUPPORT);
    } else if (type_no_flags != SOCK_STREAM && type_no_flags != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM "
                "and SOCK_DGRAM",
                type_no_flags);
        /* TODO: unclear if we should return EPROTONOSUPPORT or EINVAL */
        SYSCALL_RETURN_IF_ERROR(-EPROTONOSUPPORT);
    } else if (protocol != 0) {
        warning(
            "unsupported socket protocol \"%i\", we only support 0", protocol);
        SYSCALL_RETURN_IF_ERROR(-EPROTONOSUPPORT);
    }

    /* We are all set to create the socket. */
    DescriptorType dtype =
        (type_no_flags == SOCK_STREAM) ? DT_TCPSOCKET : DT_UDPSOCKET;
    Descriptor* desc = host_createDescriptor(sys->host, dtype);
    utility_assert(desc);

    /* Now make sure it will be valid when we operate on it. */
    int socket_fd = descriptor_getHandle(desc);
    int errorCode = _syscallhandler_validateSocketHelper(sys, socket_fd, NULL);
    if(errorCode != 0) {
        error("Unable to find socket %i that we just created.", socket_fd);
    }
    utility_assert(errorCode == 0);

    /* Set any options that were given. */
    if (type & SOCK_NONBLOCK) {
        descriptor_addFlags(desc, O_NONBLOCK);
    }
    if (type & SOCK_CLOEXEC) {
        descriptor_addFlags(desc, O_CLOEXEC);
    }

    debug("socket() returning fd %i", socket_fd);

    return (SysCallReturn){.state = SYSCALL_RETURN_DONE,
                           .retval.as_i64 = socket_fd};
}
