/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

#include "shadow.h"

enum SystemCallType {
	SCT_BIND, SCT_CONNECT, SCT_GETSOCKNAME, SCT_GETPEERNAME,
};

static Node* _system_switchInShadowContext() {
	Worker* worker = worker_getPrivate();
	if(worker->cached_plugin) {
		plugin_setShadowContext(worker->cached_plugin, TRUE);
	}
	MAGIC_ASSERT(worker->cached_node);
	return worker->cached_node;
}

static void _system_switchOutShadowContext(Node* node) {
	Worker* worker = worker_getPrivate();
	if(worker->cached_plugin) {
		plugin_setShadowContext(worker->cached_plugin, FALSE);
	}
}

/**
 * system interface to epoll library
 */

gint system_epollCreate(gint size) {
	/* size should be > 0, but can otherwise be completely ignored */
	if(size < 1) {
		errno = EINVAL;
		return -1;
	}

	/* switch into shadow and create the new descriptor */
	Node* node = _system_switchInShadowContext();
	gint handle = node_createDescriptor(node, DT_EPOLL);
	_system_switchOutShadowContext(node);

	return handle;
}

gint system_epollCreate1(gint flags) {
	/*
	 * the only possible flag is EPOLL_CLOEXEC, which means we should set
	 * FD_CLOEXEC on the new file descriptor. just ignore for now.
	 */
	if(flags != 0 && flags != EPOLL_CLOEXEC) {
		errno = EINVAL;
		return -1;
	}

	/* forward on to our regular create method */
	return system_epollCreate(1);
}

gint system_epollCtl(gint epollDescriptor, gint operation, gint fileDescriptor,
		struct epoll_event* event) {
	/*
	 * initial checks before passing on to node:
	 * EINVAL if fd is the same as epfd, or the requested operation op is not
	 * supported by this interface
	 */
	if(epollDescriptor == fileDescriptor) {
		errno = EINVAL;
		return -1;
	}

	/* switch into shadow and do the operation */
	Node* node = _system_switchInShadowContext();
	gint result = node_epollControl(node, epollDescriptor, operation, fileDescriptor, event);
	_system_switchOutShadowContext(node);

	/*
	 * When successful, epoll_ctl() returns zero. When an error occurs,
	 * epoll_ctl() returns -1 and errno is set appropriately.
	 */
	if(result != 0) {
		errno = result;
		return -1;
	} else {
		return 0;
	}
}

gint system_epollWait(gint epollDescriptor, struct epoll_event* eventArray,
		gint eventArrayLength, gint timeout) {
	/*
	 * EINVAL if maxevents is less than or equal to zero.
	 */
	if(eventArrayLength <= 0) {
		errno = EINVAL;
		return -1;
	}

	/* switch to shadow context and try to get events if we have any */
	Node* node = _system_switchInShadowContext();

	/*
	 * initial checks: we can't block, so timeout must be 0. anything else will
	 * cause a warning. if they seriously want to block by passing in -1, then
	 * return interrupt below only if we have no events.
	 *
	 * @note log while in shadow context to get node info in the log
	 */
	if(timeout != 0) {
		warning("Shadow does not block, so the '%i' millisecond timeout will be ignored", timeout);
	}

	gint nEvents = 0;
	gint result = node_epollGetEvents(node, epollDescriptor, eventArray,
			eventArrayLength, &nEvents);
	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	/*
	 * if we dont have any events and they are trying to block, tell them their
	 * timeout was interrupted.
	 */
	if(timeout != 0 && nEvents <= 0) {
		errno = EINTR;
		return -1;
	}

	/* the event count. zero is fine since they weren't expecting a timer. */
	return nEvents;
}

gint system_epollPWait(gint epollDescriptor, struct epoll_event* events,
		gint maxevents, gint timeout, const sigset_t* signalSet) {
	/*
	 * this is the same as system_epollWait, except it catches signals in the
	 * signal set. lets just assume we have no signals to worry about.
	 * forward to our regular wait method.
	 *
	 * @warning we dont handle signals
	 */
	return system_epollWait(epollDescriptor, events, maxevents, timeout);
}

/**
 * system interface to socket and IO library
 * @todo move input checking here
 */

gint system_socket(gint domain, gint type, gint protocol) {
	/* we only support non-blocking sockets, and require
	 * SOCK_NONBLOCK to be set immediately */
	gboolean isBlocking = TRUE;

	/* clear non-blocking flags if set to get true type */
	if(type & SOCK_NONBLOCK) {
		type = type & ~SOCK_NONBLOCK;
		isBlocking = FALSE;
	}
	if(type & SOCK_CLOEXEC) {
		type = type & ~SOCK_CLOEXEC;
		isBlocking = FALSE;
	}

	/* check inputs for what we support */
	if(isBlocking) {
		warning("we only support non-blocking sockets: please bitwise OR 'SOCK_NONBLOCK' with type flags");
		errno = EPROTONOSUPPORT;
		return -1;
	} else if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		warning("unsupported socket type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM", type);
		errno = EPROTONOSUPPORT;
		return -1;
	} else if(domain != AF_INET) {
		warning("trying to create socket with domain \"%i\", we only support PF_INET", domain);
		errno = EAFNOSUPPORT;
		return -1;
	}

	/* we are all set to create the socket */
	enum DescriptorType dtype = type == SOCK_STREAM ? DT_TCPSOCKET : DT_UDPSOCKET;

	Node* node = _system_switchInShadowContext();
	gint result = node_createDescriptor(node, dtype);
	_system_switchOutShadowContext(node);
	return result;
}

gint system_socketPair(gint domain, gint type, gint protocol, gint fds[2]) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_socketpair(node->vsocket_mgr, domain, type, protocol, fds);
	_system_switchOutShadowContext(node);
	return r;
}

static gint system_checkAndDirect(gint fd, const struct sockaddr* addr, socklen_t* len,
		enum SystemCallType type) {
	/* check if this is a virtual socket */
	if(fd < MIN_DESCRIPTOR){
		warning("intercepted a non-virtual descriptor");
		errno = ENOTSOCK;
		return -1;
	}

	/* check for proper addr */
	if(addr == NULL) {
		errno = EFAULT;
		return -1;
	}

	if(len == NULL || *len < sizeof(struct sockaddr_in)) {
		errno = EINVAL;
		return -1;
	}

	struct sockaddr_in* saddr = (struct sockaddr_in*) addr;
	in_addr_t ip = saddr->sin_addr.s_addr;
	in_port_t port = saddr->sin_port;
	sa_family_t family = saddr->sin_family;

	/* direct to node for further checks */
	Node* node = _system_switchInShadowContext();

	gint result = EINVAL;
	switch(type) {
		case SCT_BIND: {
			result = node_bindToInterface(node, fd, ip, port);
			break;
		}

		case SCT_CONNECT: {
			result = node_connectToPeer(node, fd, ip, port, family);
			break;
		}

		case SCT_GETPEERNAME: {
//			result = vsocket_getpeername(node->vsocket_mgr, fd, (struct sockaddr_in *) addr, len);
			break;
		}

		case SCT_GETSOCKNAME: {
//			result = vsocket_getsockname(node->vsocket_mgr, fd, (struct sockaddr_in *) addr, len);
			break;
		}

		default: {
			error("unrecognized system call type");
			break;
		}
	}

	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	return 0;
}


gint system_accept(gint fd, struct sockaddr* addr, socklen_t* len) {
	/* check if this is a virtual socket */
	if(fd < MIN_DESCRIPTOR){
		warning("intercepted a non-virtual descriptor");
		errno = ENOTSOCK;
		return -1;
	}

	/* direct to node for further checks */
	Node* node = _system_switchInShadowContext();

	gint result = 0;//node_acceptNewPeer(node);

	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	return 0;
}

gint system_accept4(gint fd, struct sockaddr* addr, socklen_t* len, gint flags) {
	/* just ignore the flags and call accept */
	return system_accept(fd, addr, len);
}

gint system_bind(gint fd, const struct sockaddr* addr, socklen_t len) {
	return system_checkAndDirect(fd, addr, &len, SCT_BIND);
}

gint system_connect(gint fd, const struct sockaddr* addr, socklen_t len) {
	return system_checkAndDirect(fd, addr, &len, SCT_CONNECT);
}

gint system_getPeerName(gint fd, struct sockaddr* addr, socklen_t* len) {
	return system_checkAndDirect(fd, addr, len, SCT_GETPEERNAME);
}

gint system_getSockName(gint fd, struct sockaddr* addr, socklen_t* len) {
	return system_checkAndDirect(fd, addr, len, SCT_GETSOCKNAME);
}

gssize system_send(gint fd, const gpointer buf, gsize n, gint flags) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_send(node->vsocket_mgr, fd, buf, n, flags);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_recv(gint fd, gpointer buf, gsize n, gint flags) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_recv(node->vsocket_mgr, fd, buf, n, flags);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_sendTo(gint fd, const gpointer buf, gsize n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_sendto(node->vsocket_mgr, fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_recvFrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_recvfrom(node->vsocket_mgr, fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_sendMsg(gint fd, const struct msghdr* message, gint flags) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_sendmsg(node->vsocket_mgr, fd, message, flags);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_recvMsg(gint fd, struct msghdr* message, gint flags) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_recvmsg(node->vsocket_mgr, fd, message, flags);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_getSockOpt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_getsockopt(node->vsocket_mgr, fd, level, optname, optval, optlen);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_setSockOpt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_setsockopt(node->vsocket_mgr, fd, level, optname, optval, optlen);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_listen(gint fd, gint backlog) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_listen(node->vsocket_mgr, fd, backlog);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_shutdown(gint fd, gint how) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_shutdown(node->vsocket_mgr, fd, how);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_read(gint fd, gpointer buf, gint numbytes) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_read(node->vsocket_mgr, fd, buf, numbytes);
	_system_switchOutShadowContext(node);
	return r;
}

gssize system_write(gint fd, const gpointer buf, gint numbytes) {
	Node* node = _system_switchInShadowContext();
	gssize r = vsocket_write(node->vsocket_mgr, fd, buf, numbytes);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_close(gint fd) {
	Node* node = _system_switchInShadowContext();
	gint r = vsocket_close(node->vsocket_mgr, fd);
	_system_switchOutShadowContext(node);
	return r;
}

/**
 * system util interface
 * @todo move input checking here
 */

time_t system_time(time_t* t) {
	Node* node = _system_switchInShadowContext();
	time_t r = vsystem_time(t);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_clockGetTime(clockid_t clk_id, struct timespec *tp) {
	Node* node = _system_switchInShadowContext();
	gint r = vsystem_clock_gettime(clk_id, tp);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_getHostName(gchar *name, size_t len) {
	Node* node = _system_switchInShadowContext();
	gint r = vsystem_gethostname(name, len);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_getAddrInfo(gchar *n, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	Node* node = _system_switchInShadowContext();
	gint r = vsystem_getaddrinfo(n, service, hgints, res);
	_system_switchOutShadowContext(node);
	return r;
}

void system_freeAddrInfo(struct addrinfo *res) {
	Node* node = _system_switchInShadowContext();
	vsystem_freeaddrinfo(res);
	_system_switchOutShadowContext(node);
}
